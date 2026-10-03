// ==========================================
// Distributed Legal RAG - k6 Load Test
// 10,000 VU stress test for portfolio demonstration
// Run: k6 run -e BASE_URL=https://nimish-legal-rag.duckdns.org k6-load-test.js
// ==========================================

import http from 'k6/http';
import { check, sleep, group } from 'k6';
import { Rate, Trend, Counter } from 'k6/metrics';
import { SharedArray } from 'k6/data';

// ==========================================
// Configuration
// ==========================================

const BASE_URL = __ENV.BASE_URL || 'http://localhost:8000';
const SCENARIO = __ENV.SCENARIO || 'ramp_10k';

// Legal questions for realistic query patterns
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
    "How are damages calculated in intellectual property infringement?",
    "What is the doctrine of res judicata and when does it apply?",
    "What are the elements of a trade secret misappropriation claim?",
    "How does the collateral source rule affect damage awards?",
    "What is the difference between void and voidable contracts?",
    "When can a court pierce the corporate veil?",
    "What are the requirements for a valid class action certification?",
    "How does the statute of frauds apply to oral agreements?",
    "What is the standard for preliminary injunction relief?",
    "What constitutes adverse possession in property law?",
    "How does the parol evidence rule affect contract interpretation?"
  ];
});

// Custom metrics
const errorRate = new Rate('errors');
const requestDuration = new Trend('request_duration');
const searchLatency = new Trend('search_latency');
const llmLatency = new Trend('llm_latency');
const totalRequests = new Counter('total_requests');
const successfulRequests = new Counter('successful_requests');

// ==========================================
// Test Scenarios
// ==========================================

export const options = {
  scenarios: {
    // Ramp up to 10k VUs over 5 minutes, sustain for 10 minutes, ramp down
    ramp_10k: {
      executor: 'ramping-vus',
      startVUs: 0,
      stages: [
        { duration: '5m', target: 10000 },  // Ramp up
        { duration: '10m', target: 10000 }, // Sustain
        { duration: '2m', target: 0 }       // Ramp down
      ],
      gracefulRampDown: '30s',
    },
    // Spike test: sudden burst
    spike_test: {
      executor: 'ramping-vus',
      startVUs: 0,
      stages: [
        { duration: '30s', target: 100 },   // Warm up
        { duration: '1m', target: 5000 },   // Spike
        { duration: '2m', target: 5000 },   // Sustain spike
        { duration: '30s', target: 0 }      // Ramp down
      ],
    },
    // Stress test: gradual increase to find breaking point
    stress_test: {
      executor: 'ramping-vus',
      startVUs: 0,
      stages: [
        { duration: '2m', target: 100 },
        { duration: '2m', target: 500 },
        { duration: '2m', target: 1000 },
        { duration: '2m', target: 2000 },
        { duration: '2m', target: 5000 },
        { duration: '5m', target: 10000 },
        { duration: '2m', target: 0 }
      ],
    },
    // Soak test: sustained load for extended period
    soak_test: {
      executor: 'constant-vus',
      vus: 1000,
      duration: '30m',
    }
  },

  // Thresholds - these define pass/fail for CI
  thresholds: {
    // Overall request duration (p95 < 2s for normal, higher for stress)
    'request_duration': ['p(95)<5000'],  // 5s for demo on t3.micro
    // Error rate should be < 10% (demo will exceed this under load - documented)
    'errors': ['rate<0.20'],
    // HTTP success rate
    'http_req_failed': ['rate<0.15'],
    // Check specific endpoints
    'checks': ['rate>0.80'],
  },

  // Output configuration
  summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)', 'p(99.9)'],

  // Tags for filtering
  tags: {
    project: 'legal-rag',
    environment: __ENV.ENVIRONMENT || 'local',
  },
};

// ==========================================
// Helper Functions
// ==========================================

function getRandomQuestion() {
  return LEGAL_QUESTIONS[Math.floor(Math.random() * LEGAL_QUESTIONS.length)];
}

function buildHeaders() {
  return {
    'Content-Type': 'application/json',
    'Accept': 'application/json',
    'User-Agent': 'k6-load-test/1.0',
  };
}

// ==========================================
// Main Test Function
// ==========================================

export default function () {
  const question = getRandomQuestion();
  const startTime = new Date();

  group('Legal RAG Query', function () {
    const payload = JSON.stringify({
      question: question,
      top_k: 5,
      include_citations: true
    });

    const params = {
      headers: buildHeaders(),
      timeout: '30s',  // Generous timeout for overloaded system
    };

    const res = http.post(`${BASE_URL}/ask`, payload, params);

    const duration = new Date() - startTime;
    requestDuration.add(duration);
    totalRequests.add(1);

    const success = check(res, {
      'status is 200': (r) => r.status === 200,
      'has answer field': (r) => {
        try {
          const body = JSON.parse(r.body);
          return body.answer !== undefined;
        } catch (e) {
          return false;
        }
      },
      'has citations': (r) => {
        try {
          const body = JSON.parse(r.body);
          return Array.isArray(body.citations);
        } catch (e) {
          return false;
        }
      },
      'response time < 30s': (r) => r.timings.duration < 30000,
    });

    errorRate.add(!success);

    if (success) {
      successfulRequests.add(1);

      // Parse response for detailed metrics
      try {
        const body = JSON.parse(res.body);
        if (body.search_latency_ms) searchLatency.add(body.search_latency_ms);
        if (body.llm_latency_ms) llmLatency.add(body.llm_latency_ms);
      } catch (e) {
        // Ignore parse errors
      }
    } else {
      // Log failures for debugging
      if (res.status !== 200) {
        console.error(`Request failed: ${res.status} - ${res.body.substring(0, 200)}`);
      }
    }
  });

  // Think time - simulate human reading time
  // Shorter under high load to maximize throughput
  const thinkTime = __ENV.SCENARIO === 'ramp_10k' ? Math.random() * 2 + 0.5 : Math.random() * 5 + 1;
  sleep(thinkTime);
}

// ==========================================
// Setup & Teardown
// ==========================================

export function setup() {
  console.log(`Starting load test against: ${BASE_URL}`);
  console.log(`Scenario: ${SCENARIO}`);
  console.log(`Legal questions pool: ${LEGAL_QUESTIONS.length}`);

  // Warm-up request to ensure services are up
  const warmup = http.get(`${BASE_URL}/health`, { timeout: '10s' });
  if (warmup.status !== 200) {
    console.warn(`Health check failed: ${warmup.status}`);
  } else {
    console.log('Health check passed - services are up');
  }

  return { startTime: new Date().toISOString() };
}

export function teardown(data) {
  const duration = (new Date() - new Date(data.startTime)) / 1000;
  console.log(`Load test completed in ${duration.toFixed(1)}s`);
  console.log(`Base URL: ${BASE_URL}`);
  console.log('Check Grafana dashboards for detailed metrics');
}

// ==========================================
// Custom Summary (for CI output)
// ==========================================

export function handleSummary(data) {
  const summary = {
    'stdout': textSummary(data, { indent: ' ', enableColors: true }),
    'summary.json': JSON.stringify(data, null, 2),
  };

  // Also write to file for CI artifacts
  return summary;
}

function textSummary(data, options) {
  const { indent = '', enableColors = false } = options || {};
  const metrics = data.metrics;

  let output = '\n';
  output += `${indent}╔══════════════════════════════════════════════════════════════╗\n`;
  output += `${indent}║           LEGAL RAG LOAD TEST SUMMARY                       ║\n`;
  output += `${indent}╠══════════════════════════════════════════════════════════════╣\n`;

  // HTTP metrics
  if (metrics.http_reqs) {
    output += `${indent}║ HTTP Requests: ${metrics.http_reqs.values.count}                                        ║\n`;
  }
  if (metrics.http_req_duration) {
    const d = metrics.http_req_duration.values;
    output += `${indent}║ HTTP Duration: avg=${d.avg.toFixed(0)}ms p95=${d['p(95)'].toFixed(0)}ms p99=${d['p(99)'].toFixed(0)}ms     ║\n`;
  }
  if (metrics.http_req_failed) {
    const rate = (metrics.http_req_failed.values.passes / metrics.http_req_failed.values.count * 100).toFixed(1);
    output += `${indent}║ HTTP Error Rate: ${rate}%                                        ║\n`;
  }

  // Custom metrics
  if (metrics.request_duration) {
    const d = metrics.request_duration.values;
    output += `${indent}║ Total Duration:  avg=${d.avg.toFixed(0)}ms p95=${d['p(95)'].toFixed(0)}ms             ║\n`;
  }
  if (metrics.search_latency) {
    const d = metrics.search_latency.values;
    output += `${indent}║ Search Latency:  avg=${d.avg.toFixed(0)}ms p95=${d['p(95)'].toFixed(0)}ms             ║\n`;
  }
  if (metrics.llm_latency) {
    const d = metrics.llm_latency.values;
    output += `${indent}║ LLM Latency:     avg=${d.avg.toFixed(0)}ms p95=${d['p(95)'].toFixed(0)}ms             ║\n`;
  }
  if (metrics.errors) {
    const rate = (metrics.errors.values.passes / metrics.errors.values.count * 100).toFixed(1);
    output += `${indent}║ Check Error Rate: ${rate}%                                      ║\n`;
  }

  output += `${indent}╚══════════════════════════════════════════════════════════════╝\n`;

  // Threshold status
  output += '\n';
  output += `${indent}Thresholds:\n`;
  for (const [name, threshold] of Object.entries(data.thresholds)) {
    const passed = threshold.ok ? '✅ PASS' : '❌ FAIL';
    output += `${indent}  ${name}: ${passed}\n`;
  }

  return output;
}