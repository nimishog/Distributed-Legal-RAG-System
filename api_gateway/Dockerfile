FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

# libgomp1 is the OpenMP runtime required by the CPU-only PyTorch wheel.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Install CPU-only PyTorch first. The default PyPI `torch` wheel pulls in the
# ~2.5 GB CUDA runtime; the CPU index avoids that and keeps the image ~1 GB.
RUN pip install --no-cache-dir --extra-index-url https://download.pytorch.org/whl/cpu torch==2.3.1

COPY requirements.txt .
# torch is intentionally absent from requirements.txt (see comment there), so
# this step cannot resolve the CUDA wheel.
RUN pip install --no-cache-dir -r requirements.txt

# Pre-load the embedding model during build to avoid runtime delay.
ENV EMBEDDING_MODEL=sentence-transformers/all-MiniLM-L6-v2 \
    EMBEDDING_DEVICE=cpu
RUN python3 -c "from sentence_transformers import SentenceTransformer; SentenceTransformer('sentence-transformers/all-MiniLM-L6-v2', device='cpu')"

COPY src/ ./src/

EXPOSE 8000

CMD ["uvicorn", "src.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]
