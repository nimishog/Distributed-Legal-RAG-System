# ==========================================
# Distributed Legal RAG - Makefile
# Common commands for development, testing, and deployment
# ==========================================

.PHONY: help build up down logs test test-cpp test-py test-frontend ingest health prometheus grafana deploy-local clean prune

# Default target
help:
	@echo "=========================================="
	@echo "Distributed Legal RAG System - Commands"
	@echo "=========================================="
	@echo ""
	@echo "Local Development:"
	@echo "  make build          - Build all Docker images locally"
	@echo "  make up             - Start full local stack (with monitoring)"
	@echo "  make up-db          - Start only database + router stack"
	@echo "  make down           - Stop all containers"
	@echo "  make logs           - Follow logs for all services"
	@echo "  make ps             - Show container status"
	@echo ""
	@echo "Testing:"
	@echo "  make test           - Run all unit tests (C++ + Python + frontend)"
	@echo "  make test-cpp       - Run C++ tests only"
	@echo "  make test-py        - Run Python tests only"
	@echo "  make test-frontend  - Run frontend tests only"
	@echo ""
	@echo "Data Ingestion:"
	@echo "  make ingest         - Run data ingestion pipeline locally"
	@echo ""
	@echo "Monitoring:"
	@echo "  make prometheus     - Open Prometheus UI (localhost:9090)"
	@echo "  make grafana        - Open Grafana UI (localhost:3000, admin/admin)"
	@echo ""
	@echo "Load Testing:"
	@echo "  make load-test      - Run k6 load test against local stack"
	@echo "  make load-test-prod - Run k6 load test against production"
	@echo ""
	@echo "Production Deployment:"
	@echo "  make deploy-local   - Deploy to local Docker (simulate prod)"
	@echo "  make clean          - Remove all containers, volumes, images"
	@echo "  make prune          - Docker system prune (nuclear option)"

# ==========================================
# Variables
# ==========================================

COMPOSE_FILE := docker-compose.local.yml
COMPOSE_PROD := docker-compose.prod.yml
K6_SCRIPT := k6-load-test.js
FRONTEND_DIR := frontend
# Prefer the `docker compose` plugin; fall back to the standalone
# `docker-compose` binary (v2) when the plugin is not installed.
DOCKER_COMPOSE := $(shell if docker compose version >/dev/null 2>&1; then echo "docker compose"; else echo "docker-compose"; fi)

# ==========================================
# Local Development
# ==========================================

build:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) build

up:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) up -d --build
	@echo ""
	@echo "Services starting... Wait ~30s for all to be healthy."
	@echo "  API Gateway:     http://localhost:8000"
	@echo "  Frontend:        http://localhost:5173"
	@echo "  Prometheus:      http://localhost:9090"
	@echo "  Grafana:         http://localhost:3000 (admin/admin)"
	@echo "  Router:          http://localhost:18084/health"

up-db:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) up -d --build \
	  postgres_0 postgres_1 postgres_2 cpp_database_0 cpp_database_1 cpp_database_2 cpp_router

down:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) down --remove-orphans

logs:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) logs -f --tail=100

ps:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) ps

# ==========================================
# Testing
# ==========================================

test: test-cpp test-py test-frontend

test-cpp:
	@echo "Running C++ tests..."
	@cd cpp_database && cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure
	@cd cpp_router && cmake -S . -B build -DBUILD_TESTS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure

test-py:
	@echo "Running Python tests..."
	@cd data_ingestion && python3 -m pytest tests/ -v
	@cd api_gateway && python3 -m pytest tests/ -v

test-frontend:
	@echo "Running Frontend tests..."
	@cd $(FRONTEND_DIR) && npm test -- --run

# ==========================================
# Data Ingestion
# ==========================================

ingest:
	@echo "Running data ingestion pipeline locally..."
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) --profile ingest run --build --rm ingestion

# ==========================================
# Monitoring
# ==========================================

prometheus:
	@echo "Opening Prometheus at http://localhost:9090"
	@xdg-open http://localhost:9090 2>/dev/null || open http://localhost:9090 2>/dev/null || echo "Please open http://localhost:9090 manually"

grafana:
	@echo "Opening Grafana at http://localhost:3000 (admin/admin)"
	@xdg-open http://localhost:3000 2>/dev/null || open http://localhost:3000 2>/dev/null || echo "Please open http://localhost:3000 manually"

# ==========================================
# Load Testing
# ==========================================

load-test:
	@echo "Running k6 load test against LOCAL stack..."
	@k6 run $(K6_SCRIPT) || echo "k6 not installed or test failed"

load-test-prod:
	@echo "Running k6 load test against PRODUCTION..."
	@read -p "Production URL (e.g., https://nimish-legal-rag.duckdns.org): " url && \
	k6 run -e BASE_URL=$$url $(K6_SCRIPT) || echo "k6 not installed or test failed"

# ==========================================
# Production Simulation (Local)
# ==========================================

deploy-local:
	@echo "Starting production-like stack locally..."
	@$(DOCKER_COMPOSE) -f $(COMPOSE_PROD) up -d --build
	@echo "Production stack running on localhost:80/443 (requires SSL certs)"

# ==========================================
# Maintenance
# ==========================================

clean:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) down -v --rmi all --remove-orphans
	$(DOCKER_COMPOSE) -f $(COMPOSE_PROD) down -v --rmi all --remove-orphans 2>/dev/null || true

prune:
	docker system prune -af --volumes

# ==========================================
# Quick Health Checks
# ==========================================

check_http = @code=$$(curl -s -o /dev/null -w "%{http_code}" $(1) 2>/dev/null); \
	case "$$code" in 200|503) echo "$(2): UP";; *) echo "$(2): DOWN";; esac

check_tcp = @nc -z localhost $(1) 2>/dev/null && echo "$(2): UP (TCP)" || echo "$(2): DOWN"

health:
	@echo "Checking service health..."
	@$(call check_http,http://localhost:8000/health,API\ Gateway)
	@$(call check_http,http://localhost:18084/health,Router)
	@$(call check_tcp,8081,DB\ Shard\ 0)
	@$(call check_tcp,8082,DB\ Shard\ 1)
	@$(call check_tcp,8083,DB\ Shard\ 2)

# ==========================================
# Database Utilities
# ==========================================

db-shell:
	docker exec -it legal-rag-postgres-0 psql -U rag_user -d legal_rag

db-logs:
	docker logs legal-rag-postgres-0 -f --tail=100

db-size:
	docker exec -it legal-rag-postgres-0 psql -U rag_user -d legal_rag -c "SELECT * FROM get_table_size('legal_chunks');"

# ==========================================
# Development Helpers
# ==========================================

shell-api:
	docker exec -it legal-rag-api bash

shell-router:
	docker exec -it legal-rag-router bash

shell-db:
	docker exec -it legal-rag-db-0 bash

rebuild-api:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) build api_gateway && $(DOCKER_COMPOSE) -f $(COMPOSE_FILE) up -d --no-deps api_gateway

rebuild-router:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) build cpp_router && $(DOCKER_COMPOSE) -f $(COMPOSE_FILE) up -d --no-deps cpp_router

rebuild-db:
	$(DOCKER_COMPOSE) -f $(COMPOSE_FILE) build cpp_database_0 cpp_database_1 cpp_database_2 && $(DOCKER_COMPOSE) -f $(COMPOSE_FILE) up -d --no-deps cpp_database_0 cpp_database_1 cpp_database_2
