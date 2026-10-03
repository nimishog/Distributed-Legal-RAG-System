#!/usr/bin/env python3
import os
import sys
import subprocess
from pathlib import Path

def run_step(name: str, module: str) -> bool:
    print(f"\n{'='*60}")
    print(f"STEP: {name}")
    print(f"{'='*60}")
    try:
        result = subprocess.run(
            [sys.executable, "-m", module],
            capture_output=False,
            text=True
        )
        if result.returncode != 0:
            print(f"FAILED: {name} (exit code: {result.returncode})")
            return False
        print(f"SUCCESS: {name}")
        return True
    except Exception as e:
        print(f"ERROR: {name} - {e}")
        return False

def main():
    os.environ.setdefault("PYTHONPATH", "/app")

    steps = [
        ("Fetch Legal Data", "src.fetch_legal_data"),
        ("Chunk Documents", "src.chunker"),
        ("Generate Embeddings", "src.embedder"),
        ("Load to Router", "src.socket_client"),
    ]

    print("Starting Legal RAG Data Ingestion Pipeline")
    print(f"Max chunks: {os.getenv('MAX_INGESTION_CHUNKS', '1000')}")

    for name, module in steps:
        if not run_step(name, module):
            print(f"\nPipeline failed at: {name}")
            sys.exit(1)

    print("\n" + "="*60)
    print("PIPELINE COMPLETED SUCCESSFULLY")
    print("="*60)

if __name__ == "__main__":
    main()