import re
import os
import json
from pathlib import Path
from typing import List, Dict, Any

MAX_CHUNKS = int(os.getenv("MAX_INGESTION_CHUNKS", "1000"))
CHUNK_SIZE = int(os.getenv("CHUNK_SIZE", "512"))
CHUNK_OVERLAP = int(os.getenv("CHUNK_OVERLAP", "50"))

DATA_DIR = Path(os.getenv("DATA_DIR", "/app/data"))
INPUT_FILE = DATA_DIR / "raw_documents.json"
OUTPUT_FILE = DATA_DIR / "chunks.json"

LEGAL_SECTION_PATTERN = re.compile(r'(?:Section|§)\s*\d+(?:\.\d+)*', re.IGNORECASE)
PARAGRAPH_SPLIT = re.compile(r'\n\s*\n')

def split_legal_text(text: str) -> List[str]:
    """Split legal text preserving section boundaries."""
    paragraphs = PARAGRAPH_SPLIT.split(text)
    chunks = []
    current_chunk = ""
    current_tokens = 0

    for para in paragraphs:
        para = para.strip()
        if not para:
            continue

        tokens = len(para.split())
        if current_tokens + tokens > CHUNK_SIZE and current_chunk:
            chunks.append(current_chunk.strip())
            overlap_text = " ".join(current_chunk.split()[-CHUNK_OVERLAP:])
            current_chunk = overlap_text + " " + para
            current_tokens = len(current_chunk.split())
        else:
            current_chunk = (current_chunk + " " + para).strip()
            current_tokens += tokens

    if current_chunk:
        chunks.append(current_chunk.strip())

    return chunks

def chunk_document(doc: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Chunk a single document preserving metadata."""
    text = doc.get("text", "")
    metadata = doc.get("metadata", {})

    chunks = split_legal_text(text)
    results = []

    for i, chunk_text in enumerate(chunks):
        chunk_metadata = metadata.copy()
        chunk_metadata.update({
            "chunk_index": i,
            "total_chunks": len(chunks),
            "char_length": len(chunk_text),
            "token_estimate": len(chunk_text.split())
        })
        results.append({
            "text": chunk_text,
            "metadata": chunk_metadata
        })

    return results

def main():
    print(f"Loading documents from {INPUT_FILE}...")
    with open(INPUT_FILE) as f:
        documents = json.load(f)

    all_chunks = []
    for doc in documents:
        all_chunks.extend(chunk_document(doc))
        if len(all_chunks) >= MAX_CHUNKS:
            all_chunks = all_chunks[:MAX_CHUNKS]
            break

    print(f"Created {len(all_chunks)} chunks (max: {MAX_CHUNKS})")

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_FILE, "w") as f:
        json.dump(all_chunks, f, indent=2)

    print(f"Saved chunks to {OUTPUT_FILE}")

if __name__ == "__main__":
    main()