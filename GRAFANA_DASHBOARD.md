# Grafana Dashboard Documentation

## Dashboard Overview

The Grafana dashboard (`grafana-dashboard.json`) monitors the Distributed Legal
RAG stack. It is provisioned automatically by `docker-compose.local.yml`:

- `provisioning/datasource.yaml` registers the Prometheus datasource.
- `provisioning/dashboards.yaml` points at `/var/lib/grafana/dashboards`.
- `grafana-dashboard.json` is mounted at `/var/lib/grafana/dashboards/home.json`.

Only metrics that are actually exported are shown. The router and C++ database
nodes do not expose Prometheus metrics, so there are no fictional
`cpp_router_*` / `cpp_database_*` panels.

## Accessing the Dashboard

```
URL: http://localhost:3000
Username: admin
Password: admin
```

## Panel Layout

### Row 1: System Resources (node-exporter)
| Panel | Metric |
|-------|--------|
| System RAM Usage | `(1 - (node_memory_MemAvailable_bytes / node_memory_MemTotal_bytes)) * 100` |
| System Swap Usage | `(node_memory_SwapTotal_bytes - node_memory_SwapFree_bytes) / node_memory_SwapTotal_bytes * 100` |
| System CPU Usage | `100 - (avg by(instance) (rate(node_cpu_seconds_total{mode="idle"}[5m])) * 100)` |
| Service Health | `up` (all scrape jobs) |

### Row 2: API Gateway
| Panel | Metric |
|-------|--------|
| API Gateway Request Rate | `sum(rate(api_gateway_http_requests_total[1m])) by (endpoint, status)` |
| API Gateway Latency | `histogram_quantile(0.50/0.95, sum(rate(api_gateway_http_request_duration_seconds_bucket[5m])) by (le, endpoint))` + LLM latency |

### Row 3: LLM & Database
| Panel | Metric |
|-------|--------|
| LLM Token Throughput | `rate(api_gateway_llm_prompt_tokens_total[1m])`, `rate(api_gateway_llm_completion_tokens_total[1m])` |
| Postgres Transaction Rate | `sum(rate(pg_stat_database_xact_commit[1m]))` |

### Row 4: Containers
| Panel | Metric |
|-------|--------|
| Container Memory Usage | `container_memory_usage_bytes{name=~"legal-rag-.*"}` (cAdvisor) |
| Postgres Connections | `pg_stat_database_numbackends` (postgres_exporter) |

## Expected Metrics for a Healthy System

| Metric | Expected Range |
|--------|----------------|
| RAM Usage | 60-85% |
| Swap Usage | 10-40% |
| CPU Usage | 20-60% |
| API Gateway p95 Latency | < 2s (includes LLM) |
| LLM Latency | 500-2000ms |
| Container Memory | < 80% of limits |
| All Services | UP (1) |

## Troubleshooting

### No Data in Panels
- Verify scrape targets: http://localhost:9090/targets
- Ensure the exporters are running: `docker compose -f docker-compose.local.yml ps`
- Check the API Gateway `/metrics` endpoint responds.

### Service Health DOWN
- Check container logs: `docker compose -f docker-compose.local.yml logs <service>`
- Verify network connectivity between containers.
