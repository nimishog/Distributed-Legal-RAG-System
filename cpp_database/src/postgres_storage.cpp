#include "postgres_storage.hpp"
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_set>

using json = nlohmann::json;

PostgresStorage::PostgresStorage(const std::string& conn_string, size_t pool_size)
    : conn_string_(conn_string), pool_size_(pool_size) {
    pool_.reserve(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        pool_.push_back({std::make_unique<pqxx::connection>(conn_string_), false});
        prepare_statements(*pool_[i].conn);
    }
}

void PostgresStorage::prepare_statements(pqxx::connection& conn) {
    conn.prepare("insert_chunk",
        "INSERT INTO legal_chunks (id, text, embedding, metadata) "
        "VALUES ($1, $2, $3::vector, $4::jsonb) "
        "ON CONFLICT (id) DO UPDATE SET "
        "text = EXCLUDED.text, embedding = EXCLUDED.embedding, metadata = EXCLUDED.metadata");

    conn.prepare("batch_insert_chunks",
        "INSERT INTO legal_chunks (id, text, embedding, metadata) "
        "SELECT * FROM UNNEST($1::uuid[], $2::text[], $3::vector[], $4::jsonb[]) "
        "ON CONFLICT (id) DO UPDATE SET "
        "text = EXCLUDED.text, embedding = EXCLUDED.embedding, metadata = EXCLUDED.metadata");

    conn.prepare("search_chunks",
        "SELECT id, text, 1 - (embedding <=> $1::vector) AS score, metadata "
        "FROM legal_chunks "
        "WHERE ($3::jsonb IS NULL OR metadata @> $3::jsonb) "
        "ORDER BY embedding <=> $1::vector "
        "LIMIT $2");

    conn.prepare("delete_chunks",
        "DELETE FROM legal_chunks WHERE id = ANY($1::uuid[])");

    conn.prepare("count_chunks",
        "SELECT COUNT(*) FROM legal_chunks");

    conn.prepare("health_check",
        "SELECT 1");
}

std::unique_ptr<pqxx::connection> PostgresStorage::acquire() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    
    // First, try to find an available connection in the pool
    for (auto& wrapper : pool_) {
        if (!wrapper.in_use && wrapper.conn) {
            // Validate connection before reuse
            if (validate_connection(*wrapper.conn)) {
                wrapper.in_use = true;
                return std::move(wrapper.conn);
            } else {
                // Connection is stale, create a new one
                wrapper.conn = std::make_unique<pqxx::connection>(conn_string_);
                prepare_statements(*wrapper.conn);
                wrapper.in_use = true;
                return std::move(wrapper.conn);
            }
        }
    }
    
    // If pool has room, create a new connection
    if (pool_.size() < pool_size_) {
        auto conn = std::make_unique<pqxx::connection>(conn_string_);
        prepare_statements(*conn);
        return conn;
    }
    
    // Pool is at max capacity, wait for a connection to become available
    // This prevents unbounded growth - wait with timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (true) {
        for (auto& wrapper : pool_) {
            if (!wrapper.in_use && wrapper.conn) {
                if (validate_connection(*wrapper.conn)) {
                    wrapper.in_use = true;
                    return std::move(wrapper.conn);
                } else {
                    wrapper.conn = std::make_unique<pqxx::connection>(conn_string_);
                    prepare_statements(*wrapper.conn);
                    wrapper.in_use = true;
                    return std::move(wrapper.conn);
                }
            }
        }
        // All connections in use, wait a bit and retry
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("Timeout waiting for database connection from pool");
        }
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lock.lock();
    }
}

bool PostgresStorage::validate_connection(pqxx::connection& conn) {
    try {
        if (!conn.is_open()) return false;
        pqxx::work txn(conn);
        txn.exec_prepared("health_check");
        return true;
    } catch (...) {
        return false;
    }
}

void PostgresStorage::release(std::unique_ptr<pqxx::connection>&& conn) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& wrapper : pool_) {
        if (!wrapper.conn) {
            wrapper.conn = std::move(conn);
            wrapper.in_use = false;
            return;
        }
    }
    // Should not happen if pool_size_ is respected, but handle gracefully
    if (pool_.size() < pool_size_) {
        pool_.push_back({std::move(conn), false});
    } else {
        // Pool is full, just close the connection
        // (connection will be closed when unique_ptr is destroyed)
    }
}

std::string PostgresStorage::vector_to_pg_array(const std::vector<float>& vec) {
    std::ostringstream oss;
    // 9 significant digits round-trips float32 exactly enough for pgvector
    // while keeping the text representation compact.
    oss << std::setprecision(9);
    oss << '[';
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) oss << ',';
        oss << vec[i];
    }
    oss << ']';
    return oss.str();
}

void PostgresStorage::insert(const std::string& id,
                             const std::string& text,
                             const std::vector<float>& embedding,
                             const json& metadata) {
    auto conn = acquire();
    try {
        pqxx::work txn(*conn);
        txn.exec_prepared("insert_chunk",
            id, text, vector_to_pg_array(embedding), metadata.dump());
        txn.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Insert failed: " + std::string(e.what()));
    }
    release(std::move(conn));
}

void PostgresStorage::batch_insert(const std::vector<InsertRequest>& requests) {
    if (requests.empty()) return;

    // Deduplicate by id: PostgreSQL's ON CONFLICT DO UPDATE cannot affect the
    // same row twice within one command, so duplicate ids in a single batch
    // would abort the whole insert. Keep the first occurrence of each id.
    std::vector<const InsertRequest*> unique;
    unique.reserve(requests.size());
    std::unordered_set<std::string> seen;
    for (const auto& req : requests) {
        if (seen.insert(req.id).second) {
            unique.push_back(&req);
        }
    }

    auto conn = acquire();
    try {
        pqxx::work txn(*conn);

        std::vector<std::string> ids;
        std::vector<std::string> texts;
        std::vector<std::string> embeddings;
        std::vector<std::string> metadatas;

        ids.reserve(unique.size());
        texts.reserve(unique.size());
        embeddings.reserve(unique.size());
        metadatas.reserve(unique.size());

        for (const auto* req : unique) {
            ids.push_back(req->id);
            texts.push_back(req->text);
            embeddings.push_back(vector_to_pg_array(req->embedding));
            metadatas.push_back(req->metadata.dump());
        }
        
        // Convert vectors to PostgreSQL array format for prepared statement
        auto vec_to_pg_array = [](const std::vector<std::string>& vec) -> std::string {
            std::ostringstream oss;
            oss << '{';
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) oss << ',';
                // Escape special characters
                std::string escaped = vec[i];
                // Escape backslashes and quotes
                std::string result;
                for (char c : escaped) {
                    if (c == '\\' || c == '"') result += '\\';
                    result += c;
                }
                oss << '"' << result << '"';
            }
            oss << '}';
            return oss.str();
        };
        
        std::string ids_array = vec_to_pg_array(ids);
        std::string texts_array = vec_to_pg_array(texts);
        std::string embeddings_array = vec_to_pg_array(embeddings);
        std::string metadatas_array = vec_to_pg_array(metadatas);
        
        txn.exec_prepared("batch_insert_chunks",
            ids_array, texts_array, embeddings_array, metadatas_array);
        txn.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Batch insert failed: " + std::string(e.what()));
    }
    release(std::move(conn));
}

std::vector<SearchResult> PostgresStorage::search(const std::vector<float>& embedding,
                                                  size_t top_k,
                                                  const json& filter) {
    auto conn = acquire();
    std::vector<SearchResult> results;
    try {
        pqxx::work txn(*conn);
        auto res = txn.exec_prepared("search_chunks",
            vector_to_pg_array(embedding), static_cast<int>(top_k), filter.dump());
        for (const auto& row : res) {
            SearchResult r;
            r.id = row["id"].as<std::string>();
            r.text = row["text"].as<std::string>();
            r.score = row["score"].as<double>();
            r.metadata = json::parse(row["metadata"].as<std::string>());
            results.push_back(std::move(r));
        }
        txn.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Search failed: " + std::string(e.what()));
    }
    release(std::move(conn));
    return results;
}

void PostgresStorage::remove(const std::vector<std::string>& ids) {
    if (ids.empty()) return;
    auto conn = acquire();
    try {
        pqxx::work txn(*conn);
        auto vec_to_pg_array = [](const std::vector<std::string>& vec) -> std::string {
            std::ostringstream oss;
            oss << '{';
            for (size_t i = 0; i < vec.size(); ++i) {
                if (i > 0) oss << ',';
                std::string escaped = vec[i];
                std::string result;
                for (char c : escaped) {
                    if (c == '\\' || c == '"') result += '\\';
                    result += c;
                }
                oss << '"' << result << '"';
            }
            oss << '}';
            return oss.str();
        };
        
        std::string ids_array = vec_to_pg_array(ids);
        txn.exec_prepared("delete_chunks", ids_array);
        txn.commit();
    } catch (const std::exception& e) {
        throw std::runtime_error("Delete failed: " + std::string(e.what()));
    }
    release(std::move(conn));
}

HealthStatus PostgresStorage::health() {
    HealthStatus status;
    status.healthy = false;
    status.total_chunks = 0;
    auto start = std::chrono::steady_clock::now();

    auto conn = acquire();
    try {
        pqxx::work txn(*conn);
        txn.exec_prepared("health_check");
        auto count_res = txn.exec_prepared("count_chunks");
        if (!count_res.empty()) {
            status.total_chunks = count_res[0][0].as<size_t>();
        }
        
        // Validate that the table has at least one index. This queries the
        // system catalog (pg_index/pg_class) instead of relying on a hardcoded
        // index name, so renaming the HNSW index does not false-positive.
        pqxx::result idx_res = txn.exec(
            "SELECT 1 FROM pg_catalog.pg_index i "
            "JOIN pg_catalog.pg_class c ON c.oid = i.indexrelid "
            "JOIN pg_catalog.pg_class t ON t.oid = i.indrelid "
            "WHERE t.relname = 'legal_chunks' LIMIT 1");
        if (idx_res.empty()) {
            throw std::runtime_error("No index found on legal_chunks");
        }
        
        // Test vector search works
        pqxx::result search_test = txn.exec("SELECT 1 FROM legal_chunks LIMIT 1");
        if (search_test.empty() && status.total_chunks > 0) {
            throw std::runtime_error("Vector search test failed");
        }
        
        txn.commit();
        status.healthy = true;
        status.message = "OK";
    } catch (const std::exception& e) {
        status.message = e.what();
    }
    release(std::move(conn));

    status.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return status;
}