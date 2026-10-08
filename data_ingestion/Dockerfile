FROM python:3.11-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DEFAULT_TIMEOUT=1000

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir --default-timeout=1000 -r requirements.txt

COPY src/ ./src/

ENV MAX_INGESTION_CHUNKS=1000 \
    EMBEDDING_MODEL=sentence-transformers/all-mpnet-base-v2 \
    EMBEDDING_BATCH_SIZE=32 \
    EMBEDDING_DEVICE=cuda \
    ROUTER_HOST=cpp_router \
    ROUTER_PORT=8080

ENTRYPOINT ["python", "-m", "src.pipeline"]