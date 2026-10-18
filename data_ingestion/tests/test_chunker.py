import pytest
from src.chunker import CHUNK_OVERLAP, CHUNK_SIZE, split_legal_text, chunk_document

def test_split_legal_text():
    text = "Section 1. This is the first section.\n\nSection 2. This is the second section with more content.\n\nSection 3. Third section."
    chunks = split_legal_text(text)
    assert len(chunks) >= 1
    assert all(len(c) > 0 for c in chunks)

def test_split_long_text():
    long_text = "\n\n".join(["This is a sentence."] * 300)
    chunks = split_legal_text(long_text)
    assert len(chunks) > 1
    # Each chunk may carry up to CHUNK_OVERLAP words of overlap from the
    # previous chunk, so the effective bound is CHUNK_SIZE + CHUNK_OVERLAP.
    assert all(len(c.split()) <= CHUNK_SIZE + CHUNK_OVERLAP for c in chunks)

def test_chunk_document():
    doc = {
        "text": "Section 1. Contract law basics.\n\nSection 2. Breach of contract remedies.",
        "metadata": {"source": "test", "topic": "contracts"}
    }
    chunks = chunk_document(doc)
    assert len(chunks) >= 1
    for chunk in chunks:
        assert "text" in chunk
        assert "metadata" in chunk
        assert chunk["metadata"]["source"] == "test"

def test_chunk_document_empty():
    doc = {"text": "", "metadata": {}}
    chunks = chunk_document(doc)
    assert len(chunks) == 0