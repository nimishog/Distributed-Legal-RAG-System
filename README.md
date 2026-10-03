# Distributed Legal RAG System

A production-grade distributed Retrieval-Augmented Generation (RAG) system for legal document query answering, demonstrating sharded vector search across 3 database nodes with consistent hashing, built in C++17 with PostgreSQL/pgvector, FastAPI, and React.

**Live Demo:** https://nimish-legal-rag.duckdns.org (running on a single t3.micro via swap-backed scale-to-zero)

![Architecture](docs/architecture.png)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              PRODUCTION (ap-south-1)                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Vercel (Frontend)                                                         │
│  distributed-rag.vercel.app                                                │
│        │                                                                    │
│        ▼                                                                    │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │ EC2 t3.micro (1 GB RAM + 2 GB Swap) - $0 Free Tier / ~$7.50/mo      │  │
│  │                                                                       │  │
│  │ ┌─────────────┐  ┌─────────────────────────────────────────────────┐ │  │
│  │ │   Nginx     │  │           FastAPI Gateway (:8000)               │ │  │
│  │ │  :80/:443   │──▶│  • Router TCP client (pool, max 2 conn)       │ │  │
│  │ │  (Certbot)  │  │  • OpenAI GPT-4o-mini                           │ │  │
│  │ └─────────────┘  │  • /ask endpoint (non-streaming)                │ │  │
│  │                  │  • Rate limit: 2 concurrent requests            │ │  │
│  │                  └────────────────────────┬────────────────────────┘ │  │
│  │                                           │ TCP :8080                │  │
│  │                  ┌────────────────────────┼────────────────────────┐ │  │
│  │                  │ docker-compose.prod.yml                       │ │  │
│  │                  │ ┌─────────┐ ┌──────────┐ ┌──────────┐        │ │  │
│  │                  │ │Postgres │ │cpp_db_0  │ │cpp_db_1  │        │ │  │
│  │                  │ │+pgvector│ │(shard 0) │ │(shard 1) │        │ │  │
│  │                  │ │ :5432   │ │ :8081    │ │ :8082    │        │ │  │
│  │                  │ └────┬────┘ └────┬─────┘ └────┬─────┘        │ │  │
│  │                  │      │           │           │               │ │  │
│  │                  │ ┌────┴────┐ ┌────┴─────┐ ┌────┴─────┐        │ │  │
│  │                  │ │cpp_db_2 │ │ cpp_router│            │        │ │  │
│  │                  │ │(shard 2)│ │  :8080    │            │        │ │  │
│  │                  │ │ :8083   │ │           │            │        │ │  │
│  │                  │ └─────────┘ └───────────┘            │        │ │  │
│  │                  └──────────────────────────────────────┘        │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Frontend** | React 18 + Vite | Chat interface, deployed on Vercel |
| **API Gateway** | FastAPI + Uvicorn | REST API, LLM orchestration, rate limiting |
| **Router** | C++17 (TCP) | Consistent hashing, connection pooling, health checks |
| **Database Nodes** | C++17 + libpqxx | 3 shards, pgvector HNSW index, binary protocol |
| **Vector DB** | PostgreSQL 16 + pgvector | 768-dim embeddings, cosine similarity |
| **Embeddings** | sentence-transformers/all-mpnet-base-v2 | 768-dim, batch inference |
| **LLM** | OpenAI GPT-4o-mini | RAG generation |
| **Monitoring** | Prometheus + Grafana | Local development observability |

---

## Demo vs. Enterprise Architecture

### Demo Architecture (This Repository)
- **Single t3.micro** (1 GB RAM + 2 GB swap)
- **2 GB swap file** prevents OOM kills during spikes
- **Rate limited** to 2 concurrent queries
- **1000 chunk cap** keeps HNSW index < 50 MB
- **Cost**: $0 (AWS Free Tier) or ~$7.50/mo
- **Purpose**: Live portfolio demo, 24/7 availability

### Enterprise Architecture (Production Scale)
```
                    ┌─────────────┐
                    │   ALB/WAF   │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌─────────┐  ┌─────────┐  ┌─────────┐
         │ FastAPI │  │ FastAPI │  │ FastAPI │  ← Auto-scaling group
         └────┬────┘  └────┬────┘  └────┬────┘
              │            │            │
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │ cpp_router  │  ← ECS Fargate Service (3+ tasks)
                    │ (CloudMap)  │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌─────────┐  ┌─────────┐  ┌─────────┐
         │ DB Shard│  │ DB Shard│  │ DB Shard│  ← ECS Fargate (3 tasks each)
         │   0     │  │   1     │  │   2     │
         └────┬────┘  └────┬────┘  └────┬────┘
              │            │            │
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │ RDS Aurora  │  ← db.r6g.xlarge (memory-optimized)
                    │ PostgreSQL  │     pgvector, read replicas
                    └─────────────┘
```
- **Horizontal scaling**: ECS Fargate auto-scaling (CPU/memory triggers)
- **Managed PostgreSQL**: RDS Aurora with pgvector, read replicas
- **Service Discovery**: AWS CloudMap for router↔DB communication
- **Connection Pooling**: PgBouncer / RDS Proxy
- **Cost**: ~$500-2000/mo depending on traffic

---

## Quick Start (Local Development)

### Prerequisites
- Docker 24+ and Docker Compose v2
- Python 3.11+ (for ingestion)
- Node.js 18+ (for frontend)
- k6 (for load testing)
- NVIDIA GPU with CUDA (optional, for fast embeddings)

### 1. Clone and Configure
```bash
git clone https://github.com/yourusername/distributed-legal-rag.git
cd distributed-legal-rag
cp .env.example .env
# Edit .env with your OpenAI API key
```

### 2. Start Full Stack
```bash
make up
```
Wait ~30 seconds for all services to become healthy.

### 3. Ingest Legal Data
```bash
make ingest
```
Downloads LegalBench-RAG-mini, chunks, embeds, and loads into the cluster.

### 4. Test the System
```bash
# Health checks
make health

# Query the API
curl -X POST http://localhost:8000/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "What is the statute of limitations for contract disputes in California?"}'

# Open Frontend
open http://localhost:5173

# Open Grafana
make grafana  # http://localhost:3000 (admin/admin)
```

---

## Project Structure

```
distributed_legal_rag/
├── .github/workflows/          # CI/CD pipelines
├── cpp_database/               # Vector DB Node (C++17)
│   ├── include/                # Headers: storage, postgres, vector_math
│   ├── src/                    # Implementation
│   └── tests/                  # GTest unit tests
├── cpp_router/                 # Sharding Router (C++17)
│   ├── include/                # Headers: router, hash_ring
│   ├── src/                    # Implementation
│   └── tests/                  # GTest unit tests
├── data_ingestion/             # Offline Pipeline (Python)
│   ├── src/                    # fetch, chunk, embed, socket_client
│   └── tests/                  # Pytest
├── api_gateway/                # FastAPI Middleware (Python)
│   ├── src/                    # routers, services, schemas
│   └── tests/                  # Pytest
├── frontend/                   # React + Vite (Vercel)
│   ├── src/                    # Components, API client
│   └── tests/                  # Vitest
├── deployment/                 # AWS Bootstrap Scripts
├── docker-compose.local.yml    # Full local dev stack
├── docker-compose.prod.yml     # Production single-server
├── docker-compose.ingestion.yml# Ingestion job
├── init_db.sql                 # pgvector schema + config
├── prometheus.yml              # Metrics scrape config
├── grafana-dashboard.json      # Pre-built dashboard
├── k6-load-test.js             # 10k VU load test
├── Makefile                    # Common commands
└── .env.example                # Environment template
```

---

## TCP Binary Protocol

All C++ services communicate via length-prefixed JSON over TCP:

```
Frame: [uint32_t length (LE)][JSON payload]

INSERT:
{"op":"insert","id":"uuid","text":"...","embedding":[768 floats],"metadata":{}}

SEARCH:
{"op":"search","embedding":[768 floats],"top_k":10,"filter":{}}

DELETE:
{"op":"delete","ids":["uuid1","uuid2"]}

RESPONSE:
{"status":"ok","results":[{"id":"uuid","text":"...","score":0.92,"metadata":{}}]}
{"status":"error","code":"NOT_FOUND|INVALID_ARG|INTERNAL","message":"..."}
```

---

## Consistent Hashing

- **Algorithm**: SHA-256(key) → uint64 (first 8 bytes)
- **Virtual Nodes**: 50 per physical shard (150 total)
- **Replication Factor**: 2 (primary + 1 replica)
- **Health Checks**: TCP ping every 10s, automatic redistribution

---

## Load Testing

Run from your local machine against the live demo:

```bash
# Install k6
brew install k6  # macOS
# or: sudo apt install k6  # Ubuntu

# Run 10k VU test
make load-test-prod
# Enter: https://nimish-legal-rag.duckdns.org
```

**Expected Results on t3.micro:**
- p50 latency: ~200-500ms
- p95 latency: ~2-5s (queueing due to 2-connection limit)
- Error rate: Increases >500 VUs (connection pool exhaustion)
- OOM risk: Swap absorbs spikes, but sustained load will thrash

This **demonstrates the breaking point honestly** — a senior engineering practice.

---

## Development Commands

```bash
make help           # Show all commands
make build          # Build all images
make up             # Start local stack
make down           # Stop stack
make logs           # Follow all logs
make test           # Run all tests
make ingest         # Run ingestion pipeline
make load-test      # Run k6 locally
make clean          # Remove everything
make prune          # Nuclear docker cleanup
```

---

## Deployment

### One-Time AWS Setup
```bash
# Configure AWS CLI
aws configure

# Run bootstrap (creates VPC, SG, EC2, IAM)
./deployment/aws_setup.sh
```

### GitHub Actions Deployment
1. Add secrets to GitHub repository:
   - `OPENAI_API_KEY`
   - `KAGGLE_USERNAME`, `KAGGLE_KEY`
   - `EC2_HOST`, `EC2_SSH_KEY`
   - `DB_PASSWORD`
2. Push to `main` branch → Automatic build + deploy
3. Manual workflows: `ingest.yml`, `load-test.yml`

### SSL Certificate (Let's Encrypt)
```bash
# On EC2 after deploy
sudo certbot --nginx -d nimish-legal-rag.duckdns.org
# Auto-renewal via cron
```

---

## Monitoring (Local Only)

| Service | URL | Credentials |
|---------|-----|-------------|
| Prometheus | http://localhost:9090 | - |
| Grafana | http://localhost:3000 | admin / admin |

Pre-built dashboard includes:
- System RAM/Swap/CPU
- Router/DB/API request rates & latency (p50/p95/p99)
- Container memory usage
- LLM token throughput
- Service health status

---

## Cost Breakdown

| Resource | Monthly Cost (ap-south-1) |
|----------|---------------------------|
| EC2 t3.micro (24/7) | $7.50 (Free Tier: $0 first 12mo) |
| EBS 30 GB | $3.00 |
| Data Transfer (est.) | $1-5 |
| **Total** | **~$10-15/mo** ($0 on Free Tier) |

---

## License

MIT License - See LICENSE file for details.

---

## Acknowledgments

- [pgvector](https://github.com/pgvector/pgvector) for vector similarity in PostgreSQL
- [sentence-transformers](https://www.sbert.net/) for embeddings
- [LegalBench](https://github.com/nguha/legalbench) for legal dataset
- [OpenAI](https://openai.com/) for GPT-4o-mini