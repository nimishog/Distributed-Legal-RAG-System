FROM python:3.11-slim

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DEFAULT_TIMEOUT=1000

# libgomp1 is the OpenMP runtime required by the CPU-only PyTorch wheel.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgomp1 \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Install CPU-only PyTorch first (avoids the multi-GB CUDA wheel from PyPI).
RUN pip install --no-cache-dir --extra-index-url https://download.pytorch.org/whl/cpu torch==2.3.1

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY src/ ./src/

ENV MAX_INGESTION_CHUNKS=1000 \
    EMBEDDING_MODEL=sentence-transformers/all-MiniLM-L6-v2 \
    EMBEDDING_BATCH_SIZE=32 \
    EMBEDDING_DEVICE=cpu \
    ROUTER_HOST=cpp_router \
    ROUTER_PORT=8080

ENTRYPOINT ["python", "-m", "src.pipeline"]
