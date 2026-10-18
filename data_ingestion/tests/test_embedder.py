import json
from unittest.mock import patch

import numpy as np

from src import embedder


def test_get_device():
    assert embedder.get_device() in ["cuda", "mps", "cpu"]


class _FakeModel:
    """Stands in for SentenceTransformer so tests need no model download."""

    def __init__(self, *args, **kwargs):
        pass

    def get_sentence_embedding_dimension(self):
        return 384

    def encode(self, texts, **kwargs):
        n = len(texts) if isinstance(texts, (list, tuple)) else 1
        return np.ones((n, 384), dtype=np.float32)


def test_embedder_pipeline_writes_384_dim_embeddings(tmp_path, monkeypatch):
    chunks = [
        {"id": "a", "text": "first legal text"},
        {"id": "b", "text": "second legal text"},
    ]
    (tmp_path / "chunks.json").write_text(json.dumps(chunks), encoding="utf-8")

    monkeypatch.setattr(embedder, "DATA_DIR", tmp_path)
    monkeypatch.setattr(embedder, "INPUT_FILE", tmp_path / "chunks.json")
    monkeypatch.setattr(embedder, "OUTPUT_FILE", tmp_path / "embedded_chunks.json")

    with patch.object(embedder, "SentenceTransformer", _FakeModel):
        embedder.main()

    out = json.loads((tmp_path / "embedded_chunks.json").read_text(encoding="utf-8"))
    assert len(out) == 2
    for chunk in out:
        assert len(chunk["embedding"]) == 384


def test_embedder_respects_max_chunks(tmp_path, monkeypatch):
    chunks = [{"id": str(i), "text": f"text {i}"} for i in range(5)]
    (tmp_path / "chunks.json").write_text(json.dumps(chunks), encoding="utf-8")

    monkeypatch.setattr(embedder, "DATA_DIR", tmp_path)
    monkeypatch.setattr(embedder, "INPUT_FILE", tmp_path / "chunks.json")
    monkeypatch.setattr(embedder, "OUTPUT_FILE", tmp_path / "embedded_chunks.json")
    monkeypatch.setattr(embedder, "MAX_CHUNKS", 2)

    with patch.object(embedder, "SentenceTransformer", _FakeModel):
        embedder.main()

    out = json.loads((tmp_path / "embedded_chunks.json").read_text(encoding="utf-8"))
    assert len(out) == 2
