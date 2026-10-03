import pytest
from src.chunker import split_legal_text, chunk_document

def test_split_legal_text():
    text = "Section 1. This is the first section.\n\nSection 2. This is the second section with more content.\n\nSection 3. Third section."
    chunks = split_legal_text(text)
    assert len(chunks) >= 1
    assert all(len(c) > 0 for c in chunks)

def test_split_long_text():
    long_text = " ".join(["This is a sentence."] * 100)
    chunks = split_legal_text(long_text)
    assert len(chunks) > 1
    assert all(len(c.split()) <= 512 for c in chunks)

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