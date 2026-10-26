#!/bin/bash
# ==========================================
# Start Prometheus + Grafana + exporters
# Run this BEFORE k6 so you can screenshot
# dashboards during the stress test.
# ==========================================

set -euo pipefail

COMPOSE_FILE="docker-compose.local.yml"

echo "Starting monitoring stack..."
docker compose -f "$COMPOSE_FILE" up -d \
  prometheus \
  grafana \
  postgres_exporter \
  node_exporter \
  cadvisor

echo "Waiting for Prometheus to be ready..."
for i in $(seq 1 30); do
  if curl -sf http://localhost:9090/-/healthy > /dev/null 2>&1; then
    echo "Prometheus is ready: http://localhost:9090"
    break
  fi
  sleep 2
done

echo "Waiting for Grafana to be ready..."
for i in $(seq 1 30); do
  if curl -sf http://localhost:3000/api/health > /dev/null 2>&1; then
    echo "Grafana is ready: http://localhost:3000 (admin/admin)"
    break
  fi
  sleep 2
done

echo ""
echo "=========================================="
echo "Monitoring stack is running."
echo "1. Open Grafana:  http://localhost:3000 (admin/admin)"
echo "2. Open Prometheus targets: http://localhost:9090/targets"
echo "3. Run your k6 test:  make load-test"
echo "4. Take screenshots of Grafana panels during the test."
echo "=========================================="
