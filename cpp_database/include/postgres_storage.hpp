#ifndef POSTGRES_STORAGE_HPP
#define POSTGRES_STORAGE_HPP

#include "storage.hpp"
#include <pqxx/pqxx>
#include <string>
#include <memory>
#include <mutex>
#include <vector>

class PostgresStorage : public StorageRepository {
public:
    explicit PostgresStorage(const std::string& conn_string, size_t pool_size = 4);
    ~PostgresStorage() override = default;

    void insert(const std::string& id,
                const std::string& text,
                const std::vector<float>& embedding,
                const nlohmann::json& metadata) override;

    std::vector<SearchResult> search(const std::vector<float>& embedding,
                                     size_t top_k,
                                     const nlohmann::json& filter = {}) override;

    void remove(const std::vector<std::string>& ids) override;

    HealthStatus health() override;

private:
    struct ConnectionWrapper {
        std::unique_ptr<pqxx::connection> conn;
        bool in_use = false;
    };

    std::unique_ptr<pqxx::connection> acquire();
    void release(std::unique_ptr<pqxx::connection>&& conn);

    std::string conn_string_;
    size_t pool_size_;
    std::vector<ConnectionWrapper> pool_;
    std::mutex pool_mutex_;

    void prepare_statements(pqxx::connection& conn);
    static std::string vector_to_pg_array(const std::vector<float>& vec);
};

#endif