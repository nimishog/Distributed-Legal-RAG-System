import pytest
import numpy as np
from src.embedder import get_device

def test_get_device():
    device = get_device()
    assert device in ["cuda", "mps", "cpu"]

def test_model_load():
    from sentence_transformers import SentenceTransformer
    model = SentenceTransformer("sentence-transformers/all-mpnet-base-v2", device="cpu")
    emb = model.encode(["test sentence"])
    assert emb.shape == (1, 768)
    assert np.isclose(np.linalg.norm(emb[0]), 1.0, atol=1e-5)

def test_batch_embedding():
    from sentence_transformers import SentenceTransformer
    model = SentenceTransformer("sentence-transformers/all-mpnet-base-v2", device="cpu")
    texts = [f"Legal text number {i}" for i in range(10)]
    embeddings = model.encode(texts, batch_size=4, normalize_embeddings=True)
    assert embeddings.shape == (10, 768)
    for emb in embeddings:
        assert np.isclose(np.linalg.norm(emb), 1.0, atol=1e-5)