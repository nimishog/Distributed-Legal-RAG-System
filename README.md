# Distributed Legal RAG System

A production-grade distributed Retrieval-Augmented Generation (RAG) system for
legal document question answering. Sharded vector search across 3 C++ database
nodes with consistent hashing, PostgreSQL/pgvector, FastAPI, and React.

- **Frontend (Vercel):** https://distributed-legal-rag.vercel.app
- **Backend (AWS):** https://nimish-legal-rag.duckdns.org (single t3.micro + 2 GB swap)

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│  React SPA (Vercel)  https://distributed-legal-rag.vercel.app        │
└───────────────────────────────┬──────────────────────────────────────┘
                                │ HTTPS
┌───────────────────────────────▼──────────────────────────────────────┐
│  EC2 t3.micro (1 GB RAM + 2 GB swap) - $0 Free Tier / ~$7.50/mo       │
│                                                                      │
│  Nginx :443 ──▶ FastAPI Gateway :8000 ──TCP──▶ C++ Router :8080      │
│                                            (consistent hashing)       │
│                          ┌───────────────┬───────────────┐           │
│                          ▼               ▼               ▼           │
│                     cpp_db_0        cpp_db_1        cpp_db_2         │
│                       :8081           :8081           :8081          │
│                          └───────────────┼───────────────┘           │
│                                          ▼                           │
│                              PostgreSQL 16 + pgvector :5432           │
└──────────────────────────────────────────────────────────────────────┘
```

Production runs **one shared PostgreSQL** instance; the three C++ nodes shard
the *write/read path* by consistent hashing and fan search out to all of them.
Local development runs **three separate PostgreSQL instances** so sharding can
be observed end to end.

### Key Components

| Component | Technology | Purpose |
|-----------|------------|---------|
| **Frontend** | React 18 + Vite | Chat UI, deployed on Vercel |
| **API Gateway** | FastAPI + Uvicorn | REST API, LLM orchestration, rate limiting |
| **Router** | C++17 (TCP) | Consistent hashing, scatter-gather search, broadcast deletes |
| **Database Nodes** | C++17 + libpqxx | 3 shards, pgvector HNSW index, binary protocol |
| **Vector DB** | PostgreSQL 16 + pgvector | 384-dim embeddings, cosine similarity |
| **Embeddings** | sentence-transformers/all-MiniLM-L6-v2 | 384-dim, async inference |
| **LLM** | OpenAI GPT-4o-mini | RAG generation |
| **Monitoring** | Prometheus + Grafana | Local development observability |

---

## Quick Start (Local Development)

### Prerequisites
- Docker 24+ and Docker Compose v2
- Python 3.11+ (for ingestion/tests)
- Node.js 18+ (for frontend tests)
- k6 (optional, for load testing)

### 1. Configure
```bash
cp .env.example .env
# Add your OpenAI API key to .env
```

### 2. Start the full stack
```bash
make up
```
This starts three independent PostgreSQL shards, the C++ router, the API
gateway, the frontend, Prometheus, Grafana, and the metric exporters. Wait
~30 seconds for health checks to pass.

### 3. Ingest legal data
```bash
make ingest
```
Downloads LegalBench, chunks it, embeds it with MiniLM (384-d), and loads it
through the router.

### 4. Test the system
```bash
make health

curl -X POST http://localhost:8000/api/ask \
  -H "Content-Type: application/json" \
  -d '{"question": "What is the statute of limitations for contract disputes in California?"}'

# Frontend:  http://localhost:5173
# Grafana:   http://localhost:3000 (admin/admin)
```

---

## TCP Binary Protocol

All C++ services communicate over TCP with a length-prefixed JSON frame. The
length prefix is a **big-endian** `uint32_t` (network byte order).

```
Frame: [uint32_t length (big-endian)][JSON payload]

INSERT:       {"op":"insert","id":"<uuid>","text":"...","embedding":[384 floats],"metadata":{}}
BATCH_INSERT: {"op":"batch_insert","chunks":[{...}, ...]}
SEARCH:       {"op":"search","embedding":[384 floats],"top_k":10,"filter":{}}
DELETE:       {"op":"delete","ids":["<uuid>", ...]}
HEALTH:       {"op":"health"}

RESPONSE:     {"status":"ok","results":[{"id":"<uuid>","text":"...","score":0.92,"metadata":{}}],"partial":false}
ERROR:        {"status":"error","code":"INVALID_ARG|UPSTREAM_FAILED|INTERNAL","message":"..."}
```

IDs must be valid UUIDs (`legal_chunks.id` is a PostgreSQL `UUID`).

### Router semantics
- **search** is a scatter-gather: the router queries every healthy shard,
  deduplicates results by id (keeping the highest score), sorts by score, and
  returns the top-k. The `partial` flag is `true` if any shard failed.
- **insert / batch_insert** are written to the primary shard and its replica.
- **delete** is broadcast to every shard.
- **health** is answered locally by the router.

---

## Consistent Hashing

- **Algorithm:** SHA-256(key) → uint64
- **Virtual nodes:** 50 per physical shard (150 total)
- **Replication factor:** 2 (primary + replica)
- **Health checks:** TCP ping every 10s; a node is marked unhealthy after 3
  consecutive failures

---

## Configuration

| Variable | Default | Purpose |
|----------|---------|---------|
| `EMBEDDING_MODEL` | `sentence-transformers/all-MiniLM-L6-v2` | 384-dim embedding model |
| `EMBEDDING_DEVICE` | `cpu` | Embedding device |
| `OPENAI_MODEL` | `gpt-4o-mini` | Generation model |
| `RATE_LIMIT_CONCURRENT` | `2` | Max concurrent `/api/ask` requests |
| `CORS_ORIGINS` | Vercel origin | Allowed browser origins (from `.env`) |
| `CORS_ORIGIN_REGEX` | _(empty)_ | Optional regex for Vercel preview deployments |
| `API_KEY` | _(empty)_ | Optional gateway API key (unset = open) |

---

## Development Commands

```bash
make help           # Show all commands
make build          # Build all images
make up             # Start local stack
make down           # Stop stack
make logs           # Follow logs
make test           # Run all tests (C++ + Python + frontend)
make ingest         # Run ingestion pipeline
make health         # Health checks
make clean          # Remove everything
```

---

## Deployment

### One-time AWS setup
```bash
aws configure
./deployment/aws_setup.sh   # VPC, subnets, SG, t3.micro, IAM role, key pair
```

### Secrets (file-based, no Swarm)
On the EC2 host:
```bash
mkdir -p secrets && chmod 700 secrets
printf '%s' "$DB_PASSWORD"    > secrets/DB_PASSWORD
printf '%s' "$OPENAI_API_KEY" > secrets/openai_api_key
printf '%s' "$API_KEY"        > secrets/api_key   # optional
```

### GitHub Actions
1. Add repository secrets: `EC2_HOST`, `EC2_SSH_KEY`, `DB_PASSWORD`,
   `OPENAI_API_KEY`, `API_KEY` (optional), `GHCR_TOKEN` (only if packages are
   private).
2. Push to `main` → images are built and pushed to GHCR, then pulled and
   deployed on EC2.
3. Manual workflows: `ingest.yml` (runs ingestion on EC2), `load-test.yml`.

### SSL (Let's Encrypt)
```bash
sudo docker run --rm \
  -v "$(pwd)/certbot/conf:/etc/letsencrypt" \
  -v "$(pwd)/certbot/www:/var/www/certbot" \
  certbot/certbot certonly --webroot -w /var/www/certbot \
  -d nimish-legal-rag.duckdns.org
```

---

## Monitoring (Local Only)

| Service | URL | Credentials |
|---------|-----|-------------|
| Prometheus | http://localhost:9090 | - |
| Grafana | http://localhost:3000 | admin / admin |

The dashboard (provisioned from `grafana-dashboard.json`) shows host
RAM/swap/CPU (node-exporter), API Gateway and LLM metrics, Postgres stats
(postgres_exporter), container memory (cAdvisor), and scrape health.

---

## Cost Breakdown

| Resource | Monthly Cost (ap-south-1) |
|----------|---------------------------|
| EC2 t3.micro (24/7) | $7.50 (Free Tier: $0 first 12 months) |
| EBS 16 GB | ~$1.60 |
| Data transfer (est.) | $1-5 |
| **Total** | **~$10-15/mo** ($0 on Free Tier) |

The 16 GB root volume is recommended because CPU-only PyTorch keeps the API
image near 1 GB; `docker image prune -f` is run after each deploy.

---

## License

MIT License - see LICENSE file for details.

---

## Acknowledgments

- [pgvector](https://github.com/pgvector/pgvector) for vector similarity in PostgreSQL
- [sentence-transformers](https://www.sbert.net/) for embeddings
- [LegalBench](https://github.com/nguha/legalbench) for the legal dataset
- [OpenAI](https://openai.com/) for GPT-4o-mini
