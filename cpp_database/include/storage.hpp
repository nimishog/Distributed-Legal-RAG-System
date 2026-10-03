#ifndef STORAGE_HPP
#define STORAGE_HPP

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct SearchResult {
    std::string id;
    std::string text;
    double score;
    json metadata;
};

struct HealthStatus {
    bool healthy;
    std::string message;
    size_t total_chunks;
    std::chrono::milliseconds latency;
};

class StorageRepository {
public:
    virtual ~StorageRepository() = default;

    virtual void insert(const std::string& id,
                        const std::string& text,
                        const std::vector<float>& embedding,
                        const json& metadata) = 0;

    virtual std::vector<SearchResult> search(const std::vector<float>& embedding,
                                             size_t top_k,
                                             const json& filter = {}) = 0;

    virtual void remove(const std::vector<std::string>& ids) = 0;

    virtual HealthStatus health() = 0;
};

using StoragePtr = std::unique_ptr<StorageRepository>;

#endif