#include "router.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <optional>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

using json = nlohmann::json;

// Logging levels
enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

// Global log level (can be set via environment variable)
static LogLevel g_log_level = []() {
    const char* env = std::getenv("LOG_LEVEL");
    if (env) {
        std::string level = env;
        if (level == "DEBUG") return LogLevel::DEBUG;
        if (level == "INFO") return LogLevel::INFO;
        if (level == "WARN") return LogLevel::WARN;
        if (level == "ERROR") return LogLevel::ERROR;
    }
    return LogLevel::INFO;
}();

#define LOG_DEBUG(msg) do { if (g_log_level <= LogLevel::DEBUG) std::cerr << "[DEBUG] " << msg << std::endl; } while(0)
#define LOG_INFO(msg) do { if (g_log_level <= LogLevel::INFO) std::cerr << "[INFO] " << msg << std::endl; } while(0)
#define LOG_WARN(msg) do { if (g_log_level <= LogLevel::WARN) std::cerr << "[WARN] " << msg << std::endl; } while(0)
#define LOG_ERROR(msg) do { if (g_log_level <= LogLevel::ERROR) std::cerr << "[ERROR] " << msg << std::endl; } while(0)

// Constants
constexpr size_t MAX_FRAME_SIZE = 16 * 1024 * 1024;  // 16 MB
constexpr size_t MAX_HTTP_HEADER_SIZE = 16 * 1024;   // 16 KB
constexpr size_t DEFAULT_POOL_SIZE = 8;
constexpr std::chrono::seconds HEALTH_INTERVAL{10};
constexpr std::chrono::seconds IDLE_TIMEOUT{60};
constexpr std::chrono::seconds SHUTDOWN_TIMEOUT{30};
constexpr std::chrono::seconds DEDUP_TTL{5};
constexpr std::chrono::seconds DNS_TTL{300};

namespace {

// Read exactly n bytes. Returns false on EOF/error/partial read.
bool read_all(int fd, void* buffer, size_t n) {
    char* ptr = static_cast<char*>(buffer);
    size_t total = 0;
    while (total < n) {
        ssize_t r = ::read(fd, ptr + total, n - total);
        if (r <= 0) return false;
        total += static_cast<size_t>(r);
    }
    return true;
}

// Write exactly n bytes. Returns false on error/partial write.
bool write_all(int fd, const void* buffer, size_t n) {
    const char* ptr = static_cast<const char*>(buffer);
    size_t total = 0;
    while (total < n) {
        ssize_t r = ::write(fd, ptr + total, n - total);
        if (r <= 0) return false;
        total += static_cast<size_t>(r);
    }
    return true;
}

}  // namespace

Router::Router(int port, const std::vector<Node>& nodes, int http_port, size_t worker_threads, size_t max_request_queue)
    : port_(port), http_port_(http_port > 0 ? http_port : port + 1), server_fd_(-1), http_server_fd_(-1), running_(false),
      worker_pool_(worker_threads, max_request_queue) {
    const char* pool_size_env = std::getenv("ROUTER_POOL_SIZE");
    pool_size_per_node_ = pool_size_env ? std::stoul(pool_size_env) : DEFAULT_POOL_SIZE;
    for (const auto& node : nodes) {
        hash_ring_.add_node(node.id, node.host, node.port);
    }
}

Router::~Router() {
    stop();
}

ThreadPool::ThreadPool(size_t num_threads, size_t max_queue_size)
    : max_queue_size_(max_queue_size), stop_(false) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
                    if (stop_ && queue_.empty()) return;
                    task = std::move(queue_.front());
                    queue_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
        condition_.notify_all();
    }
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void Router::start() {
    // Main binary protocol server
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("Failed to bind");
    }

    if (listen(server_fd_, 128) < 0) {
        throw std::runtime_error("Failed to listen");
    }

    // HTTP health check server
    http_server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (http_server_fd_ == -1) {
        throw std::runtime_error("Failed to create HTTP socket");
    }

    setsockopt(http_server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
    setsockopt(http_server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

    sockaddr_in http_addr{};
    http_addr.sin_family = AF_INET;
    http_addr.sin_addr.s_addr = INADDR_ANY;
    http_addr.sin_port = htons(http_port_);

    if (bind(http_server_fd_, (sockaddr*)&http_addr, sizeof(http_addr)) < 0) {
        throw std::runtime_error("Failed to bind HTTP port");
    }

    if (listen(http_server_fd_, 128) < 0) {
        throw std::runtime_error("Failed to listen on HTTP port");
    }

    running_ = true;
    health_thread_ = std::thread(&Router::health_check_loop, this);
    http_thread_ = std::thread(&Router::run_http_accept_loop, this);
    std::cout << "Router listening on port " << port_ << " (binary), " << http_port_ << " (HTTP)" << std::endl;
    run_accept_loop();
}

void Router::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (http_server_fd_ != -1) {
        close(http_server_fd_);
        http_server_fd_ = -1;
    }

    // Wait for in-flight requests to complete (with timeout)
    auto deadline = std::chrono::steady_clock::now() + shutdown_timeout_;
    while (in_flight_requests_.load() > 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (health_thread_.joinable()) {
        health_thread_.join();
    }
    if (http_thread_.joinable()) {
        http_thread_.join();
    }
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (auto& [node_id, pool] : connection_pools_) {
        for (auto& conn : pool) {
            if (conn->fd != -1) close(conn->fd);
        }
    }
}

void Router::health_check_loop() {
    while (running_) {
        std::this_thread::sleep_for(health_interval_);
        if (!running_) break;

        auto nodes = hash_ring_.get_all_nodes();
        for (const auto& node : nodes) {
            auto endpoint = resolve_hostname(node.host);
            if (!endpoint.has_value()) {
                hash_ring_.mark_healthy(node.id, false);
                continue;
            }
            endpoint->port = node.port;

            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                hash_ring_.mark_healthy(node.id, false);
                continue;
            }

            sockaddr_in addr = create_sockaddr(endpoint.value());

            struct timeval tv{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            bool healthy = connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
            if (healthy) {
                std::string req = R"({"op":"health"})";
                uint32_t len = htonl(static_cast<uint32_t>(req.size()));
                if (write_all(fd, &len, sizeof(len)) && write_all(fd, req.data(), req.size())) {
                    uint32_t resp_len = 0;
                    if (read_all(fd, &resp_len, sizeof(resp_len))) {
                        resp_len = ntohl(resp_len);
                        if (resp_len > MAX_FRAME_SIZE) {
                            healthy = false;
                        } else {
                            std::string resp(resp_len, '\0');
                            if (read_all(fd, &resp[0], resp_len)) {
                                try {
                                    json j = json::parse(resp);
                                    healthy = j.value("healthy", false);
                                } catch (...) {
                                    healthy = false;
                                }
                            } else {
                                healthy = false;
                            }
                        }
                    } else {
                        healthy = false;
                    }
                } else {
                    healthy = false;
                }
            }
            close(fd);
            hash_ring_.mark_healthy(node.id, healthy);
        }
    }
}

void Router::run_http_accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(http_server_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (running_) {
                LOG_WARN("HTTP accept error: " << std::strerror(errno));
            }
            break;
        }
        handle_http_client(client_fd);
    }
}

void Router::handle_http_client(int client_fd) {
    std::string request;
    if (!read_http_request(client_fd, request)) {
        close(client_fd);
        return;
    }

    bool is_health = request.rfind("GET /health", 0) == 0 ||
                     request.rfind("GET /healthz", 0) == 0 ||
                     request.rfind("GET /ready", 0) == 0 ||
                     request.rfind("GET /live", 0) == 0;

    if (is_health) {
        auto nodes = hash_ring_.get_all_nodes();
        bool has_healthy = false;
        for (const auto& node : nodes) {
            if (node.healthy) {
                has_healthy = true;
                break;
            }
        }
        std::string body = has_healthy
            ? "{\"status\":\"ok\",\"healthy\":true,\"service\":\"router\"}"
            : "{\"status\":\"degraded\",\"healthy\":false,\"service\":\"router\",\"reason\":\"no healthy database nodes\"}";
        std::string response = "HTTP/1.1 " + std::string(has_healthy ? "200 OK" : "503 Service Unavailable") + "\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        write_all(client_fd, response.c_str(), response.size());
    } else {
        std::string body = "{\"status\":\"error\",\"code\":\"NOT_FOUND\",\"message\":\"Not found\"}";
        std::string response = "HTTP/1.1 404 Not Found\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        write_all(client_fd, response.c_str(), response.size());
    }
    close(client_fd);
}

void Router::run_accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (running_) {
                LOG_WARN("Accept error: " << std::strerror(errno));
            }
            if (errno == EMFILE || errno == ENFILE || errno == ENOMEM || errno == ECONNABORTED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            break;
        }

        if (!worker_pool_.enqueue([this, client_fd]() {
            handle_client(client_fd);
        })) {
            std::string err = R"({"status":"error","code":"TOO_MANY_REQUESTS","message":"Server busy"})";
            write_frame(client_fd, err);
            close(client_fd);
        }
    }
}

bool Router::send_to_node(const Node& node, const std::string& request, std::string& response) {
    auto conn = acquire_connection(node);
    if (!conn) {
        return false;
    }
    bool ok = false;
    if (write_frame(conn->fd, request)) {
        ok = read_frame(conn->fd, response);
    }
    release_connection(conn);
    return ok;
}

UpstreamConnection* Router::acquire_connection(const Node& node) {
    UpstreamConnection* conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        auto& pool = connection_pools_[node.id];
        auto now = std::chrono::steady_clock::now();

        // Clean up idle/invalid connections first.
        auto it = pool.begin();
        while (it != pool.end()) {
            if (!(*it)->in_use &&
                (now - (*it)->last_used > idle_timeout_ || !(*it)->valid || (*it)->fd == -1)) {
                it = pool.erase(it);
            } else {
                ++it;
            }
        }

        // Prefer an already-connected, free connection.
        for (auto& c : pool) {
            if (!c->in_use && c->valid && c->fd != -1) {
                c->in_use = true;
                c->last_used = now;
                return c.get();
            }
        }

        // Otherwise take any free slot.
        for (auto& c : pool) {
            if (!c->in_use) {
                c->in_use = true;
                c->last_used = now;
                conn = c.get();
                break;
            }
        }

        // Create a new connection if under the per-node limit.
        if (!conn && pool.size() < pool_size_per_node_) {
            pool.push_back(std::make_unique<UpstreamConnection>());
            conn = pool.back().get();
            conn->node_id = node.id;
            conn->in_use = true;
            conn->last_used = now;
        }
    }

    if (!conn) {
        return nullptr;
    }

    // Connect outside the pool lock so DNS/connect latency does not stall
    // every other request.
    if (conn->fd == -1 || !conn->valid) {
        if (!ensure_connected(conn, node)) {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            conn->in_use = false;
            conn->valid = false;
            return nullptr;
        }
    }
    return conn;
}

void Router::release_connection(UpstreamConnection* conn) {
    if (conn) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        conn->in_use = false;
        conn->last_used = std::chrono::steady_clock::now();
    }
}

bool Router::ensure_connected(UpstreamConnection* conn, const Node& node) {
    if (conn->fd != -1 && conn->valid) {
        return true;
    }

    if (conn->fd != -1) {
        close(conn->fd);
        conn->fd = -1;
    }

    conn->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->fd < 0) {
        conn->valid = false;
        return false;
    }

    int opt = 1;
    setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    auto endpoint = resolve_hostname(node.host);
    if (!endpoint.has_value()) {
        close(conn->fd);
        conn->fd = -1;
        conn->valid = false;
        return false;
    }
    endpoint->port = node.port;

    sockaddr_in addr = create_sockaddr(endpoint.value());

    struct timeval tv{5, 0};
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(conn->fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(conn->fd);
        conn->fd = -1;
        conn->valid = false;
        return false;
    }

    conn->valid = true;
    return true;
}

bool Router::read_frame(int fd, std::string& payload) {
    uint32_t len = 0;
    if (!read_all(fd, &len, sizeof(len))) return false;

    len = ntohl(len);
    if (len > MAX_FRAME_SIZE) return false;

    payload.resize(len);
    if (len > 0 && !read_all(fd, &payload[0], len)) return false;
    return true;
}

bool Router::read_http_request(int fd, std::string& out_request) {
    std::string request;
    char buf[1024];
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        request.append(buf, static_cast<size_t>(n));
        if (request.size() > MAX_HTTP_HEADER_SIZE) return false;
    }
    out_request = request;
    return true;
}

std::optional<ResolvedEndpoint> Router::resolve_hostname(const std::string& hostname) {
    std::lock_guard<std::mutex> lock(dns_mutex_);
    auto now = std::chrono::steady_clock::now();

    auto it = dns_cache_.find(hostname);
    if (it != dns_cache_.end() && (now - it->second.resolved_at) < dns_ttl_) {
        return it->second;
    }

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    int result = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (result != 0 || !res) {
        if (it != dns_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    char ip[INET_ADDRSTRLEN];
    const struct sockaddr_in* addr = reinterpret_cast<const struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
    freeaddrinfo(res);

    ResolvedEndpoint endpoint{ip, 0, now};
    dns_cache_[hostname] = endpoint;
    return endpoint;
}

struct sockaddr_in Router::create_sockaddr(const ResolvedEndpoint& endpoint) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(endpoint.port));
    inet_pton(AF_INET, endpoint.ip.c_str(), &addr.sin_addr);
    return addr;
}

bool Router::write_frame(int fd, const std::string& payload) {
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    if (!write_all(fd, &len, sizeof(len))) return false;
    if (!payload.empty() && !write_all(fd, payload.data(), payload.size())) return false;
    return true;
}

namespace {

// RAII guard so the in-flight counter is always decremented, even on early
// returns or exceptions.
struct InFlightGuard {
    std::atomic<int>& counter;
    explicit InFlightGuard(std::atomic<int>& c) : counter(c) { counter.fetch_add(1); }
    ~InFlightGuard() { counter.fetch_sub(1); }
};

std::vector<Node> healthy_nodes(const std::vector<Node>& nodes) {
    std::vector<Node> result;
    for (const auto& n : nodes) {
        if (n.healthy) result.push_back(n);
    }
    return result;
}

}  // namespace

void Router::handle_client(int client_fd) {
    InFlightGuard guard(in_flight_requests_);

    while (running_) {
        std::string request;
        if (!read_frame(client_fd, request)) {
            break;
        }
        if (request.empty()) {
            break;
        }

        json req;
        try {
            req = json::parse(request);
        } catch (...) {
            write_frame(client_fd, R"({"status":"error","code":"INVALID_ARG","message":"Invalid JSON"})");
            continue;
        }

        std::string op = req.value("op", "");

        // Request deduplication: return a recent identical response if present.
        std::string cache_key = request;
        {
            std::lock_guard<std::mutex> lock(dedup_mutex_);
            auto now = std::chrono::steady_clock::now();
            auto it = request_cache_.find(cache_key);
            if (it != request_cache_.end() && it->second.expires_at > now) {
                write_frame(client_fd, it->second.response);
                continue;
            }
        }

        std::string response;
        bool handled = false;

        if (op == "health") {
            response = R"({"status":"ok","healthy":true})";
            write_frame(client_fd, response);
            shutdown(client_fd, SHUT_WR);
            handled = true;
        } else if (op == "search") {
            handled = handle_search(req, request, response);
        } else if (op == "insert" || op == "batch_insert") {
            handled = handle_write(req, request, response);
        } else if (op == "delete") {
            handled = handle_delete(request, response);
        } else {
            response = R"({"status":"error","code":"INVALID_ARG","message":"Unknown operation"})";
            handled = true;
        }

        if (!handled) {
            continue;
        }

        if (op == "health") {
            break;
        }

        // Cache successful responses for deduplication.
        if (response.find("\"status\":\"ok\"") != std::string::npos) {
            std::lock_guard<std::mutex> lock(dedup_mutex_);
            auto expires_at = std::chrono::steady_clock::now() + dedup_ttl_;
            request_cache_[cache_key] = RequestCacheEntry{response, expires_at};
        }

        if (!write_frame(client_fd, response)) {
            break;
        }
    }
    close(client_fd);
}

bool Router::handle_search(const json& req, const std::string& raw_request, std::string& response) {
    size_t top_k = req.value("top_k", 10);

    auto nodes = healthy_nodes(hash_ring_.get_all_nodes());
    if (nodes.empty()) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"No healthy database nodes"})";
        return true;
    }

    std::vector<std::string> raw_responses(nodes.size());
    std::vector<std::future<bool>> futures;
    futures.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [this, &nodes, &raw_request, &raw_responses, i]() {
            return send_to_node(nodes[i], raw_request, raw_responses[i]);
        }));
    }

    bool any_success = false;
    bool all_success = true;
    for (auto& f : futures) {
        if (f.get()) any_success = true;
        else all_success = false;
    }

    if (!any_success) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"All upstreams failed"})";
        return true;
    }

    // Merge results across shards, deduplicating by id and keeping the max score.
    std::map<std::string, json> merged;
    for (const auto& raw : raw_responses) {
        if (raw.empty()) continue;
        try {
            json resp = json::parse(raw);
            if (resp.value("status", "") != "ok") continue;
            for (const auto& item : resp.value("results", json::array())) {
                std::string id = item.value("id", "");
                if (id.empty()) continue;
                double score = item.value("score", 0.0);
                auto it = merged.find(id);
                if (it == merged.end() || score > it->second.value("score", 0.0)) {
                    merged[id] = item;
                }
            }
        } catch (...) {
            continue;
        }
    }

    std::vector<json> items;
    items.reserve(merged.size());
    for (auto& [id, item] : merged) {
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const json& a, const json& b) {
        return a.value("score", 0.0) > b.value("score", 0.0);
    });
    if (items.size() > top_k) {
        items.resize(top_k);
    }

    json resp;
    resp["status"] = "ok";
    resp["results"] = items;
    resp["partial"] = !all_success;
    response = resp.dump();
    return true;
}

bool Router::handle_write(const json& req, const std::string& raw_request, std::string& response) {
    std::string key;
    if (req.value("op", "") == "insert") {
        key = req.value("id", "");
    } else {
        auto chunks = req.value("chunks", json::array());
        if (!chunks.empty()) key = chunks[0].value("id", "");
    }

    auto targets = hash_ring_.get_nodes(key, 2);
    if (targets.empty()) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"No healthy nodes"})";
        return true;
    }

    int successes = 0;
    for (const auto& tgt : targets) {
        std::string node_response;
        if (send_to_node(tgt, raw_request, node_response)) {
            // Only count the write as successful if the shard actually returned
            // status "ok". send_to_node() returns true on any response, so a
            // shard-side error would otherwise be masked as success.
            try {
                if (json::parse(node_response).value("status", "") == "ok") {
                    successes++;
                }
            } catch (...) {
                // Unparseable response: treat as failure.
            }
        }
    }

    if (successes == 0) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"All upstreams failed"})";
        return true;
    }

    json resp;
    resp["status"] = "ok";
    resp["partial"] = successes < static_cast<int>(targets.size());
    response = resp.dump();
    return true;
}

bool Router::handle_delete(const std::string& raw_request, std::string& response) {
    auto nodes = healthy_nodes(hash_ring_.get_all_nodes());
    if (nodes.empty()) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"No healthy database nodes"})";
        return true;
    }

    int successes = 0;
    for (const auto& tgt : nodes) {
        std::string node_response;
        if (send_to_node(tgt, raw_request, node_response)) {
            try {
                if (json::parse(node_response).value("status", "") == "ok") {
                    successes++;
                }
            } catch (...) {
                // Unparseable response: treat as failure.
            }
        }
    }

    if (successes == 0) {
        response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"All upstreams failed"})";
        return true;
    }

    json resp;
    resp["status"] = "ok";
    resp["partial"] = successes < static_cast<int>(nodes.size());
    response = resp.dump();
    return true;
}
