import os
import json
import torch
import numpy as np
from pathlib import Path
from sentence_transformers import SentenceTransformer
from typing import List, Dict, Any

MAX_CHUNKS = int(os.getenv("MAX_INGESTION_CHUNKS", "1000"))
BATCH_SIZE = int(os.getenv("EMBEDDING_BATCH_SIZE", "32"))
MODEL_NAME = os.getenv("EMBEDDING_MODEL", "sentence-transformers/all-mpnet-base-v2")
DEVICE = os.getenv("EMBEDDING_DEVICE", "cuda")

DATA_DIR = Path(os.getenv("DATA_DIR", "/app/data"))
INPUT_FILE = DATA_DIR / "chunks.json"
OUTPUT_FILE = DATA_DIR / "embedded_chunks.json"

def get_device() -> str:
    if DEVICE == "cuda" and torch.cuda.is_available():
        return "cuda"
    elif DEVICE == "mps" and torch.backends.mps.is_available():
        return "mps"
    return "cpu"

def main():
    device = get_device()
    print(f"Using device: {device}")
    print(f"Loading model: {MODEL_NAME}")

    model = SentenceTransformer(MODEL_NAME, device=device)
    print(f"Model loaded. Embedding dimension: {model.get_sentence_embedding_dimension()}")

    print(f"Loading chunks from {INPUT_FILE}...")
    with open(INPUT_FILE) as f:
        chunks = json.load(f)

    chunks = chunks[:MAX_CHUNKS]
    texts = [c["text"] for c in chunks]

    print(f"Embedding {len(texts)} texts in batches of {BATCH_SIZE}...")
    embeddings = []
    for i in range(0, len(texts), BATCH_SIZE):
        batch = texts[i:i + BATCH_SIZE]
        batch_embeddings = model.encode(
            batch,
            batch_size=BATCH_SIZE,
            show_progress_bar=False,
            convert_to_numpy=True,
            normalize_embeddings=True
        )
        embeddings.append(batch_embeddings)
        if device == "cuda":
            torch.cuda.empty_cache()
        if (i // BATCH_SIZE + 1) % 10 == 0:
            print(f"  Processed {i + len(batch)} / {len(texts)} texts")
    embeddings = np.vstack(embeddings)

    for i, chunk in enumerate(chunks):
        chunk["embedding"] = embeddings[i].tolist()

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_FILE, "w") as f:
        json.dump(chunks, f, indent=2)

    print(f"Saved {len(chunks)} embedded chunks to {OUTPUT_FILE}")
    print(f"Embedding shape: {embeddings.shape}")

if __name__ == "__main__":
    main()