-- ==========================================
-- Distributed Legal RAG - Database Initialization
-- pgvector extension, schema, indexes, and low-memory PostgreSQL config
-- ==========================================

-- Enable pgvector extension
CREATE EXTENSION IF NOT EXISTS vector;

-- ==========================================
-- PostgreSQL Configuration for 10k VU Stress Test
-- Each cpp_database node uses DB_POOL_SIZE=256, so each Postgres instance
-- must accept at least 256 concurrent connections plus overhead.
-- Target: 1 GB container limit per Postgres instance.
-- ==========================================

-- Connection limits (raised from 40 to 300 to accommodate DB_POOL_SIZE=256)
ALTER SYSTEM SET max_connections = 300;
ALTER SYSTEM SET superuser_reserved_connections = 10;

-- Memory settings (conservative for 1 GB RAM containers)
ALTER SYSTEM SET shared_buffers = '128MB';
ALTER SYSTEM SET effective_cache_size = '512MB';
ALTER SYSTEM SET maintenance_work_mem = '64MB';
ALTER SYSTEM SET work_mem = '16MB';
ALTER SYSTEM SET huge_pages = 'off';

-- Parallelism (disable for low memory / high concurrency)
ALTER SYSTEM SET max_parallel_workers_per_gather = 0;
ALTER SYSTEM SET max_parallel_workers = 1;
ALTER SYSTEM SET max_parallel_maintenance_workers = 1;

-- WAL settings (reduce disk I/O)
ALTER SYSTEM SET wal_buffers = '16MB';
ALTER SYSTEM SET min_wal_size = '80MB';
ALTER SYSTEM SET max_wal_size = '1GB';
ALTER SYSTEM SET checkpoint_completion_target = 0.9;

-- Query planner (optimize for limited resources)
ALTER SYSTEM SET random_page_cost = 1.1;
ALTER SYSTEM SET effective_io_concurrency = 200;
ALTER SYSTEM SET default_statistics_target = 100;

-- Logging (minimal for production)
ALTER SYSTEM SET log_min_messages = 'WARNING';
ALTER SYSTEM SET log_min_error_statement = 'ERROR';
ALTER SYSTEM SET log_statement = 'none';
ALTER SYSTEM SET log_duration = 'off';

-- Reload configuration
SELECT pg_reload_conf();

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
CREATE OR REPLACE FUNCTION get_index_size(index_name TEXT)
RETURNS TEXT AS $$
DECLARE
    size_bytes BIGINT;
BEGIN
    SELECT pg_relation_size(index_name) INTO size_bytes;
    RETURN pg_size_pretty(size_bytes);
END;
$$ LANGUAGE plpgsql;

-- Function to get table + index sizes
CREATE OR REPLACE FUNCTION get_table_size(table_name TEXT)
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

-- Verify config
-- SHOW shared_buffers;
-- SHOW work_mem;
-- SHOW max_connections;````

### What to do next

1. Apply the `init_db.sql` change above (rebuild the Postgres containers so the new `max_connections` takes effect).
2. On Windows, create `%USERPROFILE%\.wslconfig`:
   ```ini
   [wsl2]
   memory=24GB
   processors=8
   swap=4GB
   ```
   Then run `wsl --shutdown` in PowerShell and restart Docker Desktop.
3. Inside WSL, run `ulimit -n 65536` before `docker compose up` and before running k6.
4. Start the stack and run:
   ```bash
   k6 run -e SCENARIO=ramp_10k k6-load-test.js
   ```

After those changes, the codebase should be able to attempt a 10k VU stress test on your 32 GB / RTX 4060 machine.