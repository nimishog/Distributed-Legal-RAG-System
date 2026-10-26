# Performance Testing Guide

This document describes how to run k6 load and stress tests against the
Distributed Legal RAG local stack, what hardware is required, and how to
interpret the results.

## Stack Overview

| Component | Technology | Local Port |
|-----------|------------|------------|
| API Gateway | FastAPI + Uvicorn (8 workers) | 8000 |
| C++ Router | C++17 TCP binary protocol | 8080 |
| C++ Database Nodes | C++17 + libpqxx (3 shards) | 8081–8083 |
| PostgreSQL | pgvector 16 (3 instances) | 5433–5435 |
| Frontend | React + Vite (Nginx) | 5173 |
| Monitoring | Prometheus + Grafana | 9090 / 3000 |

All C++ services communicate over a length-prefixed JSON binary protocol.
The API Gateway is the entry point for `/api/ask` requests.

## Prerequisites for 10k VU Stress Test

### 1. WSL2 Memory Allocation (Windows host)

Create or edit `%USERPROFILE%\.wslconfig`:

```ini
[wsl2]
memory=24GB
processors=8
swap=4GB
```

Then run in PowerShell:

```powershell
wsl --shutdown
```

Restart Docker Desktop. The local stack plus k6 at 10k VUs requires roughly
24–28 GB of RAM allocated to WSL2. The default WSL2 memory limit (~50% of
physical RAM) is insufficient.

### 2. File Descriptor Limit

Inside WSL2, before starting the stack and k6:

```bash
ulimit -n 65536
```

10k concurrent sockets require far more than the default 1024 file
descriptors.

### 3. Recreate PostgreSQL Volumes

If the stack was previously started with the old `init_db.sql` (default
`max_connections=100`), recreate the volumes so the new `max_connections=300`
takes effect:

```bash
docker compose -f docker-compose.local.yml down -v
docker compose -f docker-compose.local.yml up -d
```

## k6 Scenarios

| Scenario | VUs | Duration | Purpose |
|----------|-----|----------|---------|
| `smoke` | 5 | 3 min | Verify all endpoints work under light load |
| `load` | 50 | 9 min | Steady-state throughput and p95 latency |
| `stress` | 300 | 5 min | Find the breaking point where errors spike |
| `ramp_10k` | 10,000 | 17 min | Full stress test (requires 24–28 GB WSL RAM) |
| `spike_test` | 5,000 | 3 min | Sudden traffic spike |
| `soak_test` | 1,000 | 30 min | Long-running stability |

### Run a smoke test

```bash
k6 run -e SCENARIO=smoke k6-load-test.js
```

### Run the 10k VU stress test

```bash
ulimit -n 65536
k6 run -e SCENARIO=ramp_10k k6-load-test.js
```

### Run against production

```bash
k6 run -e BASE_URL=https://nimish-legal-rag.duckdns.org -e SCENARIO=smoke k6-load-test.js
```

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BASE_URL` | `http://localhost:8000` | Target API Gateway URL |
| `SCENARIO` | `ramp_10k` | k6 scenario name |
| `MAX_VUS` | `10000` | Max virtual users for ramp scenarios |
| `API_KEY` | `` | Optional gateway API key (unset = open) |
| `THINK_MIN` | `0.5` | Minimum think time between requests (seconds) |
| `THINK_MAX` | `2.5` | Maximum think time between requests (seconds) |
| `P95_THRESHOLD` | `30000` | p95 latency threshold in ms |
| `ERROR_RATE_THRESHOLD` | `0.60` | Max acceptable error rate |
| `REQUEST_TIMEOUT_MS` | `60000` | Per-request timeout in ms |

## Codebase Concurrency Tuning (Already Applied)

The following limits were raised specifically for the 10k VU test:

| Setting | File | Old Value | New Value |
|---------|------|-----------|-----------|
| `RATE_LIMIT_CONCURRENT` | `docker-compose.local.yml` | 16 | 5000 |
| Semaphore acquire timeout | `api_gateway/src/routers/ask.py` | 2s | 60s |
| Uvicorn workers | `api_gateway/Dockerfile` | 1 | 8 |
| `ROUTER_WORKER_THREADS` | `docker-compose.local.yml` | 32 | 512 |
| `ROUTER_MAX_QUEUE` | `docker-compose.local.yml` | 1000 | 50000 |
| `ROUTER_POOL_SIZE` | `docker-compose.local.yml` | 16 | 1000 |
| `DB_WORKER_THREADS` | `docker-compose.local.yml` | 16 | 256 |
| `DB_POOL_SIZE` | `docker-compose.local.yml` | 16 | 256 |
| `DB_MAX_QUEUE` | `docker-compose.local.yml` | 1000 | 50000 |
| PostgreSQL `max_connections` | `docker-compose.local.yml` | 100 | 300 |

## Expected Metrics

Run the smoke test first to establish a baseline:

```bash
k6 run -e SCENARIO=smoke k6-load-test.js
```

### Healthy smoke test indicators

- `http_req_duration` p95: under 5000 ms
- `errors` rate: under 10%
- `checks` pass rate: over 95%
- No `503 Server busy` responses

### 10k VU stress test expectations

At 10k VUs, the system is expected to degrade gracefully:

- Some requests will queue behind the 5000-connection semaphore.
- p95 latency may exceed 30 seconds during peak ramp.
- Error rate may rise above 20–40% as the stack saturates.
- The goal is to observe **where** the system breaks (router queue, DB
  connections, or gateway memory), not to achieve 100% success.

## Grafana Dashboards (Local Only)

| Service | URL | Credentials |
|---------|-----|-------------|
| Prometheus | http://localhost:9090 | — |
| Grafana | http://localhost:3000 | admin / admin |

Key panels to watch during a test:

- **API Gateway Request Rate** (`api_gateway_http_requests_total`)
- **API Gateway Latency** (`api_gateway_http_request_duration_seconds`)
- **Container Memory Usage** (`container_memory_usage_bytes`)
- **Postgres Connections** (`pg_stat_database_numbackends`)

## Bottlenecks to Watch

1. **API Gateway semaphore** — `RATE_LIMIT_CONCURRENT=5000` caps concurrent
   `/api/ask` requests. Above this, requests queue for up to 60s before
   returning `503 Server busy`.
2. **Router connection pool** — `ROUTER_POOL_SIZE=1000` limits concurrent
   upstream connections per database node.
3. **PostgreSQL connections** — `max_connections=300` per shard must exceed
   `DB_POOL_SIZE=256` plus overhead.
4. **WSL2 memory** — 10k VUs plus the full stack can exceed 20 GB RAM.
5. **File descriptors** — `ulimit -n` must be raised for 10k concurrent
   sockets.

## Interpreting Results

Save the JSON summary for later comparison:

```bash
k6 run -e SCENARIO=load --out json=results-load-50vus.json k6-load-test.js
```

Compare runs by checking:

- `metrics.http_req_duration.p(95)` — tail latency
- `metrics.errors.rate` — failure rate
- `metrics.checks.rate` — assertion pass rate
- `metrics.total_requests.count` — total completed requests

If `errors.rate` spikes while `checks.rate` stays high, the system is
rejecting requests cleanly (semaphore backpressure) rather than crashing.

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| `503 Server busy` on every request | Semaphore exhausted | Raise `RATE_LIMIT_CONCURRENT` or lower VUs |
| `connection refused` on router | Router not healthy | Check `docker compose logs cpp_router` |
| `max_connections` exceeded | Postgres limit too low | Verify `max_connections=300` is active |
| `EMFILE` / `ENFILE` errors | File descriptor limit | Run `ulimit -n 65536` |
| OOM kills in WSL2 | Insufficient RAM | Allocate 24–28 GB in `.wslconfig` |
| Tests hang during ramp | k6 socket exhaustion | Lower `MAX_VUS` or raise `ulimit -n` |

## Resume-Worthy Metrics Example

After a successful 50 VU load test, you can document:

> "Load-tested a distributed RAG system with k6; achieved 50 concurrent users
> with p95 latency under 5s and <1% error rate on a 32 GB local stack.
> Identified and documented bottlenecks in API gateway semaphore, router
> thread pool, and database connection pooling."
