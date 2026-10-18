#ifndef ROUTER_HPP
#define ROUTER_HPP

#include "hash_ring.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <functional>
#include <unordered_set>
#include <nlohmann/json.hpp>

struct UpstreamConnection {
    int fd = -1;
    std::string node_id;
    std::chrono::steady_clock::time_point last_used;
    bool in_use = false;
    bool valid = false;
};

struct ResolvedEndpoint {
    std::string ip;
    int port;
    std::chrono::steady_clock::time_point resolved_at;
};

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads, size_t max_queue_size = 1000);
    ~ThreadPool();

    template<typename F>
    bool enqueue(F&& f) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= max_queue_size_) {
            return false;  // Queue full, reject request
        }
        queue_.emplace(std::forward<F>(f));
        lock.unlock();
        condition_.notify_one();
        return true;
    }

    size_t queue_size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    size_t max_queue_size_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
};

class Router {
public:
    Router(int port, const std::vector<Node>& nodes, int http_port = 0, size_t worker_threads = 4, size_t max_request_queue = 1000);
    ~Router();

    void start();
    void stop();

private:
    void run_accept_loop();
    void run_http_accept_loop();
    void health_check_loop();
    void handle_client(int client_fd);
    void handle_http_client(int client_fd);
    bool peek_http_request(int fd);
    bool read_http_request(int fd, std::string& payload);
    bool read_frame(int fd, std::string& payload);
    bool write_frame(int fd, const std::string& payload);

    UpstreamConnection* acquire_connection(const Node& node);
    void release_connection(UpstreamConnection* conn);
    bool ensure_connected(UpstreamConnection* conn, const Node& node);
    bool send_to_node(const Node& node, const std::string& request, std::string& response);

    bool handle_search(const nlohmann::json& req, const std::string& raw_request, std::string& response);
    bool handle_write(const nlohmann::json& req, const std::string& raw_request, std::string& response);
    bool handle_delete(const std::string& raw_request, std::string& response);

    std::optional<ResolvedEndpoint> resolve_hostname(const std::string& hostname);
    struct sockaddr_in create_sockaddr(const ResolvedEndpoint& endpoint);

    int port_;
    int http_port_;
    int server_fd_;
    int http_server_fd_;
    HashRing hash_ring_;
    std::atomic<bool> running_;

    std::mutex pool_mutex_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<UpstreamConnection>>> connection_pools_;
    size_t pool_size_per_node_;

    std::mutex dns_mutex_;
    std::unordered_map<std::string, ResolvedEndpoint> dns_cache_;
    const std::chrono::seconds dns_ttl_{300};  // 5 minutes

    ThreadPool worker_pool_;

    std::thread health_thread_;
    std::thread http_thread_;
    const std::chrono::seconds health_interval_{10};
    const std::chrono::seconds idle_timeout_{60};  // 60 seconds idle timeout
    const std::chrono::seconds shutdown_timeout_{30};  // 30 seconds graceful shutdown timeout
    std::atomic<int> in_flight_requests_{0};

    // Request deduplication cache
    struct RequestCacheEntry {
        std::string response;
        std::chrono::steady_clock::time_point expires_at;
    };
    std::mutex dedup_mutex_;
    std::unordered_map<std::string, RequestCacheEntry> request_cache_;
    const std::chrono::seconds dedup_ttl_{5};  // 5 second TTL for deduplication
};

#endif