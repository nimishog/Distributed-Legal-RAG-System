-- ==========================================
-- Distributed Legal RAG - Database Initialization
-- pgvector extension, schema, indexes, and utilities
-- ==========================================

-- Enable pgvector extension
CREATE EXTENSION IF NOT EXISTS vector;

-- ==========================================
-- Legal Chunks Table with pgvector
-- ==========================================

CREATE TABLE IF NOT EXISTS legal_chunks (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    text TEXT NOT NULL,
    embedding VECTOR(384) NOT NULL,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Comments for documentation
COMMENT ON TABLE legal_chunks IS 'Legal document chunks with 384-dim embeddings (all-MiniLM-L6-v2)';
COMMENT ON COLUMN legal_chunks.id IS 'Unique chunk identifier';
COMMENT ON COLUMN legal_chunks.text IS 'Original text content';
COMMENT ON COLUMN legal_chunks.embedding IS '384-dimensional vector embedding';
COMMENT ON COLUMN legal_chunks.metadata IS 'JSON metadata: source, page, section, etc.';
COMMENT ON COLUMN legal_chunks.created_at IS 'Insertion timestamp';

-- ==========================================
-- Indexes
-- ==========================================

-- HNSW index for cosine similarity search
-- m=16: lower memory footprint than default 32
-- ef_construction=64: balanced build speed vs recall
CREATE INDEX IF NOT EXISTS idx_legal_chunks_embedding_hnsw
ON legal_chunks USING hnsw (embedding vector_cosine_ops)
WITH (m = 16, ef_construction = 64);

-- GIN index for metadata filtering
CREATE INDEX IF NOT EXISTS idx_legal_chunks_metadata
ON legal_chunks USING gin (metadata);

-- Partial index for common filters (e.g., source type)
CREATE INDEX IF NOT EXISTS idx_legal_chunks_source
ON legal_chunks ((metadata->>'source'))
WHERE metadata ? 'source';

-- ==========================================
-- Utility Functions
-- ==========================================

-- Function to get index size
CREATE OR REPLACE FUNCTION get_index_size(index_name regclass)
RETURNS TEXT AS $$
DECLARE
    size_bytes BIGINT;
BEGIN
    SELECT pg_relation_size(index_name) INTO size_bytes;
    RETURN pg_size_pretty(size_bytes);
END;
$$ LANGUAGE plpgsql;

-- Function to get table + index sizes
CREATE OR REPLACE FUNCTION get_table_size(table_name regclass)
RETURNS TABLE (table_size TEXT, index_size TEXT, total_size TEXT) AS $$
BEGIN
    RETURN QUERY SELECT
        pg_size_pretty(pg_relation_size(table_name)) AS table_size,
        pg_size_pretty(pg_total_relation_size(table_name) - pg_relation_size(table_name)) AS index_size,
        pg_size_pretty(pg_total_relation_size(table_name)) AS total_size;
END;
$$ LANGUAGE plpgsql;

-- ==========================================
-- Vector Search Helper (optional, for direct SQL testing)
-- ==========================================

-- Example: Find top-k similar chunks
-- SELECT id, text, metadata, 1 - (embedding <=> $1) AS similarity
-- FROM legal_chunks
-- ORDER BY embedding <=> $1
-- LIMIT 10;

-- ==========================================
-- Verification Queries
-- ==========================================

-- Verify extension
-- SELECT * FROM pg_extension WHERE extname = 'vector';

-- Verify table
-- \d legal_chunks

-- Verify indexes
-- \di idx_legal_chunks_*