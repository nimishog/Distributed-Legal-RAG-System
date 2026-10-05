# Grafana Dashboard Documentation

## Dashboard Overview

The Grafana dashboard (`grafana-dashboard.json`) provides real-time monitoring for the Distributed Legal RAG system with 13 panels across 4 rows.

## Accessing the Dashboard

### Local Development
```
URL: http://localhost:3000
Username: admin
Password: admin
```

### Production
- Deploy Grafana alongside the stack or use Grafana Cloud
- Import `grafana-dashboard.json` via Dashboards → Import

## Panel Layout

### Row 1: System Resources
| Panel | Metric | Thresholds |
|-------|--------|------------|
| System RAM Usage | `(1 - (node_memory_MemAvailable_bytes / node_memory_MemTotal_bytes)) * 100` | Green < 70%, Yellow < 90%, Red ≥ 90% |
| System Swap Usage | `(node_memory_SwapTotal_bytes - node_memory_SwapFree_bytes) / node_memory_SwapTotal_bytes * 100` | Green < 50%, Yellow < 80%, Red ≥ 80% |
| System CPU Usage | `100 - (avg by(instance) (rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100)` | Green < 70%, Yellow < 90%, Red ≥ 90% |

### Row 2: Router Metrics
| Panel | Metric | Description |
|-------|--------|-------------|
| Router Request Rate | `sum(rate(cpp_router_requests_total[1m])) by (op, status)` | Requests/sec by operation (insert/search/health) and status |
| Router Latency | `histogram_quantile(0.50/0.95/0.99, sum(rate(cpp_router_latency_seconds_bucket[5m])) by (le, op))` | p50, p95, p99 latency by operation |

### Row 3: Database Metrics (per Shard)
| Panel | Metric | Description |
|-------|--------|-------------|
| DB Request Rate | `sum(rate(cpp_database_requests_total[1m])) by (shard_id, op, status)` | Requests/sec per shard |
| DB Latency | `histogram_quantile(0.50/0.95, sum(rate(cpp_database_latency_seconds_bucket[5m])) by (le, shard_id, op))` | p50, p99 latency per shard |

### Row 4: API Gateway & LLM Metrics
| Panel | Metric | Description |
|-------|--------|-------------|
| API Gateway Request Rate | `sum(rate(api_gateway_requests_total[1m])) by (endpoint, status)` | Requests/sec by endpoint |
| API Gateway Latency | `histogram_quantile(0.50/0.95, sum(rate(api_gateway_latency_seconds_bucket[5m])) by (le, endpoint))` + `api_gateway_llm_latency_seconds` | API latency + LLM latency |
| LLM Token Throughput | `rate(api_gateway_llm_prompt_tokens_total[1m])`, `rate(api_gateway_llm_completion_tokens_total[1m])` | Tokens/sec for prompt and completion |

### Row 5: Container & Service Health
| Panel | Metric | Description |
|-------|--------|-------------|
| Container Memory Usage | `container_memory_usage_bytes{name=~"legal-rag-.*"}` | Memory usage per container |
| C++ Services Health | `up{job=~"cpp-.*"}` | UP/DOWN status for router and databases |
| Core Services Health | `up{job=~"api-gateway|postgres"}` | UP/DOWN status for API Gateway and PostgreSQL |

## Expected Metrics for Healthy System

### Local Development (t3.micro equivalent)
| Metric | Expected Range |
|--------|----------------|
| RAM Usage | 60-85% |
| Swap Usage | 10-40% |
| CPU Usage | 20-60% |
| Router p95 Latency | < 100ms |
| Database p95 Latency | < 50ms |
| API Gateway p95 Latency | < 2s (with LLM) |
| LLM Latency | 500-2000ms |
| Container Memory | < 80% of limits |
| All Services | UP (1) |

### Under Load (10k VU)
| Metric | Expected Range |
|--------|----------------|
| RAM Usage | 85-95% |
| Swap Usage | 40-80% |
| CPU Usage | 80-100% |
| Router p95 Latency | < 500ms |
| Database p95 Latency | < 200ms |
| API Gateway p95 Latency | < 10s |
| LLM Latency | 2-5s |
| Error Rate | < 10% (due to rate limiting) |

## Capturing Screenshots for README

### 1. Baseline (Idle System)
```bash
# Start stack
make up
# Wait 2 minutes for stabilization
# Open Grafana at http://localhost:3000
# Time range: "Last 5 minutes"
# Capture full dashboard screenshot
# Save as docs/grafana-idle.png
```

### 2. Under Load (k6 Test)
```bash
# Run load test
make load-test
# While test is running, refresh Grafana
# Time range: "Last 15 minutes"
# Capture full dashboard screenshot
# Save as docs/grafana-load.png
```

### 3. Key Panels Close-ups
Capture individual panels for documentation:
- Router Latency (p50/p95/p99)
- Database Latency per Shard
- API Gateway Latency + LLM Latency
- Container Memory Usage
- Service Health Status

### 4. Production Screenshots
If running against production:
```bash
# Run load test against production
make load-test-prod
# Capture production dashboard
# Save as docs/grafana-prod-load.png
```

## Screenshot Guidelines

- **Resolution**: 1920x1080 minimum
- **Time Range**: Set appropriate range (5m for idle, 15m for load)
- **Refresh**: 10s (default) or 5s for load tests
- **Annotations**: Add text annotations for key events (deployment, load test start/end)
- **File Format**: PNG for crisp text
- **Naming**: `grafana-{state}-{component}.png` (e.g., `grafana-load-router-latency.png`)

## Adding Screenshots to README

```markdown
## Monitoring

### System Overview (Idle)
![System Idle](docs/grafana-idle.png)

### Under Load (10k VU)
![System Load](docs/grafana-load.png)

### Router Latency (p50/p95/p99)
![Router Latency](docs/grafana-load-router-latency.png)

### Database Latency per Shard
![DB Latency](docs/grafana-load-db-latency.png)

### API Gateway + LLM Latency
![API Latency](docs/grafana-load-api-latency.png)
```

## Alerting Rules (Recommended)

```yaml
groups:
  - name: legal-rag-alerts
    rules:
      - alert: ServiceDown
        expr: up{job=~"legal-rag.*"} == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Service {{ $labels.job }} is down"

      - alert: HighMemoryUsage
        expr: (container_memory_usage_bytes / container_spec_memory_limit_bytes) > 0.9
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "Container {{ $labels.name }} memory > 90%"

      - alert: HighErrorRate
        expr: rate(api_gateway_requests_total{status=~"5.."}[5m]) > 0.1
        for: 2m
        labels:
          severity: critical
        annotations:
          summary: "API Gateway error rate > 10%"
```

## Troubleshooting

### No Data in Panels
- Verify Prometheus is scraping targets: http://localhost:9090/targets
- Check service health endpoints are responding
- Ensure metrics endpoints (`/metrics`) are exposed

### High Latency
- Check container resource limits
- Review database connection pool settings
- Verify LLM API response times

### Service Health DOWN
- Check container logs: `docker logs legal-rag-{service}`
- Verify network connectivity between services
- Check health check endpoints manually