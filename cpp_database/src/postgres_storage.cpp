#include "postgres_storage.hpp"
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>
#include <chrono>

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

    conn.prepare("search_chunks",
        "SELECT id, text, 1 - (embedding <=> $1::vector) AS score, metadata "
        "FROM legal_chunks "
        "ORDER BY embedding <=> $1::vector "
        "LIMIT $2");

    conn.prepare("delete_chunks",
        "DELETE FROM legal_chunks WHERE id = ANY($1)");

    conn.prepare("count_chunks",
        "SELECT COUNT(*) FROM legal_chunks");

    conn.prepare("health_check",
        "SELECT 1");
}

std::unique_ptr<pqxx::connection> PostgresStorage::acquire() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    for (auto& wrapper : pool_) {
        if (!wrapper.in_use) {
            wrapper.in_use = true;
            return std::move(wrapper.conn);
        }
    }
    auto conn = std::make_unique<pqxx::connection>(conn_string_);
    prepare_statements(*conn);
    return conn;
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
    pool_.push_back({std::move(conn), false});
}

std::string PostgresStorage::vector_to_pg_array(const std::vector<float>& vec) {
    std::ostringstream oss;
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

std::vector<SearchResult> PostgresStorage::search(const std::vector<float>& embedding,
                                                  size_t top_k,
                                                  const json& filter) {
    (void)filter;
    auto conn = acquire();
    std::vector<SearchResult> results;
    try {
        pqxx::work txn(*conn);
        auto res = txn.exec_prepared("search_chunks",
            vector_to_pg_array(embedding), static_cast<int>(top_k));
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
        pqxx::array_parser parser;
        std::string array_str = "{";
        for (size_t i = 0; i < ids.size(); ++i) {
            if (i > 0) array_str += ",";
            array_str += "\"" + ids[i] + "\"";
        }
        array_str += "}";
        txn.exec_prepared("delete_chunks", array_str);
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