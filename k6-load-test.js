// ==========================================
// Distributed Legal RAG - k6 Load Test
// ==========================================
//
// WARNING: The default scenario below is ramp_10k (10,000 VUs).
// Running 10k VUs requires:
//   1. WSL2 memory allocated to at least 24-28 GB (edit %USERPROFILE%\.wslconfig
//      on Windows, then run: wsl --shutdown).
//   2. ulimit -n raised to at least 65536 inside WSL for 10k concurrent sockets.
//   3. Codebase concurrency limits raised (see docker-compose.local.yml and
//      api_gateway/src/routers/ask.py).
//
// For a safe smoke test on a smaller machine, run:
//   k6 run -e SCENARIO=smoke k6-load-test.js
// ==========================================

import http from 'k6/http';
import { check, sleep, group } from 'k6';
import { Rate, Trend, Counter } from 'k6/metrics';
import { SharedArray } from 'k6/data';
import { textSummary } from 'https://jslib.k6.io/k6-summary/0.0.2/index.js';

// ==========================================
// Configuration
// ==========================================

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8000';
const SCENARIO = __ENV.SCENARIO || 'ramp_10k';

const MAX_VUS = Number(__ENV.MAX_VUS || 10000);
const SOAK_VUS = Number(__ENV.SOAK_VUS || 1000);
const SMOKE_VUS = Number(__ENV.SMOKE_VUS || 5);
const SPIKE_VUS = Number(__ENV.SPIKE_VUS || 5000);

const API_KEY = __ENV.API_KEY || '';

const THINK_MIN = Number(__ENV.THINK_MIN || 0.5);
const THINK_MAX = Number(__ENV.THINK_MAX || 2.5);

const P95_THRESHOLD = Number(__ENV.P95_THRESHOLD || 30000);
const ERROR_RATE_THRESHOLD = Number(__ENV.ERROR_RATE_THRESHOLD || 0.60);

const REQUEST_TIMEOUT_MS = Number(__ENV.REQUEST_TIMEOUT_MS || 60000);

// Legal questions
const LEGAL_QUESTIONS = new SharedArray('questions', function() {
  return [
    "What is the statute of limitations for breach of contract in California?",
    "What are the elements of a negligence claim in New York?",
    "How does the discovery rule affect statute of limitations in federal court?",
    "What is the difference between compensatory and punitive damages?",
    "When does attorney-client privilege apply to corporate communications?",
    "What are the requirements for a valid arbitration agreement?",
    "How does the Erie doctrine apply in diversity jurisdiction?",
    "What is the standard for summary judgment in federal court?",
    "What constitutes a material breach of contract?",
    "How are damages calculated in intellectual property infringement?"
  ];
});

// Custom metrics
const errorRate = new Rate('errors');
const requestDuration = new Trend('request_duration');
const searchLatency = new Trend('search_latency');
const llmLatency = new Trend('llm_latency');
const totalRequests = new Counter('total_requests');
const successfulRequests = new Counter('successful_requests');
const emptyResults = new Rate('empty_results');

const SCENARIOS = {
  smoke: {
    executor: 'constant-vus',
    vus: SMOKE_VUS,
    duration: __ENV.SMOKE_DURATION || '3m',
  },
  load: {
    executor: 'ramping-vus',
    startVUs: 0,
    stages: [
      { duration: '2m', target: 50 },
      { duration: '5m', target: 50 },
      { duration: '2m', target: 0 },
    ],
  },
  stress: {
    executor: 'ramping-vus',
    startVUs: 0,
    stages: [
      { duration: '1m', target: 100 },
      { duration: '2m', target: 200 },
      { duration: '1m', target: 300 },
      { duration: '1m', target: 0 },
    ],
  },
  ramp_10k: {
    executor: 'ramping-vus',
    startVUs: 0,
    stages: [
      { duration: '5m', target: MAX_VUS },
      { duration: '10m', target: MAX_VUS },
      { duration: '2m', target: 0 },
    ],
    gracefulRampDown: '30s',
  },
  spike_test: {
    executor: 'ramping-vus',
    startVUs: 0,
    stages: [
      { duration: '30s', target: 100 },
      { duration: '1m', target: SPIKE_VUS },
      { duration: '2m', target: SPIKE_VUS },
      { duration: '30s', target: 0 },
    ],
  },
  soak_test: {
    executor: 'constant-vus',
    vus: SOAK_VUS,
    duration: __ENV.SOAK_DURATION || '30m',
  },
};

if (!SCENARIOS[SCENARIO]) {
  throw new Error(`Unknown SCENARIO: ${SCENARIO}`);
}

export const options = {
  scenarios: {
    [SCENARIO]: SCENARIOS[SCENARIO],
  },
  thresholds: {
    'http_req_duration': [`p(95)<${P95_THRESHOLD}`],
    'errors': [`rate<${ERROR_RATE_THRESHOLD}`],
    'http_req_failed': [`rate<${ERROR_RATE_THRESHOLD}`],
    'checks': ['rate>0.50'],
    'empty_results': ['rate<0.10'],
  },
  summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)'],
};

function getRandomQuestion() {
  return LEGAL_QUESTIONS[Math.floor(Math.random() * LEGAL_QUESTIONS.length)];
}

function buildHeaders() {
  const h = {
    'Content-Type': 'application/json',
    'Accept': 'application/json',
  };
  if (API_KEY) h['X-API-Key'] = API_KEY;
  return h;
}

export default function () {
  const question = getRandomQuestion();

  group('Legal RAG Query', function () {
    const payload = JSON.stringify({
      question: question,
      top_k: 5,
      include_citations: true
    });

    const params = {
      headers: buildHeaders(),
      timeout: `${REQUEST_TIMEOUT_MS}ms`,
    };

    const res = http.post(`${BASE_URL}/api/ask`, payload, params);

    requestDuration.add(res.timings.duration);
    totalRequests.add(1);

    let body = null;
    try {
      body = res.body ? JSON.parse(res.body) : null;
    } catch (e) {
      body = null;
    }

    const success = check(res, {
      'status is 200': (r) => r.status === 200,
      'has answer': () => body && typeof body.answer === 'string' && body.answer.length > 0,
      'has citations array': () => body && Array.isArray(body.citations),
      'response within timeout': (r) => r.timings.duration < REQUEST_TIMEOUT_MS,
      'has search_results_count': () => body && typeof body.search_results_count === 'number',
    });

    errorRate.add(!success);

    if (success) {
      successfulRequests.add(1);
      if (body && body.search_latency_ms) searchLatency.add(body.search_latency_ms);
      if (body && body.llm_latency_ms) llmLatency.add(body.llm_latency_ms);
      if (body && Array.isArray(body.citations)) {
        emptyResults.add(body.citations.length === 0);
      }
    } else {
      if (res.status !== 200 && Math.random() < 0.01) {
        console.error(`Request failed: ${res.status} - ${String(res.body).substring(0, 200)}`);
      }
    }
  });

  const thinkTime = THINK_MIN + Math.random() * (THINK_MAX - THINK_MIN);
  sleep(thinkTime);
}

export function setup() {
  console.log(`Starting load test against: ${BASE_URL}`);
  console.log(`Scenario: ${SCENARIO}`);
  console.log(`MAX_VUS: ${MAX_VUS}`);

  if (SCENARIO === 'ramp_10k') {
    console.warn('WARNING: Running 10k VUs. Ensure WSL has 24-28 GB RAM allocated and stack concurrency limits are raised.');
  }

  const health = http.get(`${BASE_URL}/health`, { timeout: '10s' });
  if (health.status !== 200) {
    throw new Error(`Preflight failed: /health returned ${health.status}`);
  }
  console.log('Health check passed - services are up');

  if (__ENV.PREFLIGHT_ASK === 'true') {
    const res = http.post(`${BASE_URL}/api/ask`, JSON.stringify({ question: 'preflight', top_k: 1 }), { headers: buildHeaders(), timeout: '10s' });
    if (res.status !== 200) {
      throw new Error(`Preflight /api/ask failed: ${res.status}`);
    }
  }

  return { startTime: new Date().toISOString() };
}

export function teardown(data) {
  const duration = (new Date() - new Date(data.startTime)) / 1000;
  console.log(`Load test completed in ${duration.toFixed(1)}s`);
}

export function handleSummary(data) {
  const summary = {
    'stdout': textSummary(data, { indent: ' ', enableColors: true }),
    'summary.json': JSON.stringify(data, null, 2),
  };
  return summary;
}
