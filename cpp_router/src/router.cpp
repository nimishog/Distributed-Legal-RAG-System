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
#include <nlohmann/json.hpp>
#include <openssl/sha.h>

using json = nlohmann::json;

Router::Router(int port, const std::vector<Node>& nodes)
    : port_(port), server_fd_(-1), running_(false) {
    const char* pool_size_env = std::getenv("ROUTER_POOL_SIZE");
    pool_size_per_node_ = pool_size_env ? std::stoul(pool_size_env) : 8;
    for (const auto& node : nodes) {
        hash_ring_.add_node(node.id, node.host, node.port);
    }
}

Router::~Router() {
    stop();
}

void Router::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        throw std::runtime_error("Failed to bind");
    }

    if (listen(server_fd_, 10) < 0) {
        throw std::runtime_error("Failed to listen");
    }

    running_ = true;
    health_thread_ = std::thread(&Router::health_check_loop, this);
    std::cout << "Router listening on port " << port_ << std::endl;
    run_accept_loop();
}

void Router::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (health_thread_.joinable()) {
        health_thread_.join();
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
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) continue;

            sockaddr_in addr = create_sockaddr(node.host, node.port);
            if (addr.sin_addr.s_addr == 0) {
                close(fd);
                hash_ring_.mark_healthy(node.id, false);
                continue;
            }

            struct timeval tv{2, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            bool healthy = connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
            if (healthy) {
                std::string req = R"({"op":"health"})";
                uint32_t len = __builtin_bswap32(static_cast<uint32_t>(req.size()));
                if (write(fd, &len, sizeof(len)) == sizeof(len) &&
                    write(fd, req.data(), req.size()) == static_cast<ssize_t>(req.size())) {
                    uint32_t resp_len;
                    if (read(fd, &resp_len, sizeof(resp_len)) == sizeof(resp_len)) {
                        resp_len = __builtin_bswap32(resp_len);
                        std::string resp(resp_len, '\0');
                        if (read(fd, &resp[0], resp_len) == static_cast<ssize_t>(resp_len)) {
                            try {
                                json j = json::parse(resp);
                                healthy = j.value("healthy", false);
                            } catch (...) {
                                healthy = false;
                            }
                        } else {
                            healthy = false;
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

void Router::run_accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) std::cerr << "Accept error" << std::endl;
            continue;
        }
        handle_client(client_fd);
    }
}

UpstreamConnection* Router::acquire_connection(const Node& node) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    auto& pool = connection_pools_[node.id];
    for (auto& conn : pool) {
        if (!conn->in_use) {
            conn->in_use = true;
            conn->last_used = std::chrono::steady_clock::now();
            if (!ensure_connected(conn.get())) {
                conn->in_use = false;
                continue;
            }
            return conn.get();
        }
    }
    if (pool.size() < pool_size_per_node_) {
        pool.push_back(std::make_unique<UpstreamConnection>());
        auto conn = pool.back().get();
        conn->node_id = node.id;
        conn->in_use = true;
        if (ensure_connected(conn)) {
            return conn;
        }
        conn->in_use = false;
    }
    return nullptr;
}

void Router::release_connection(UpstreamConnection* conn) {
    if (conn) {
        conn->in_use = false;
        conn->last_used = std::chrono::steady_clock::now();
    }
}

bool Router::ensure_connected(UpstreamConnection* conn) {
    if (conn->fd != -1) return true;

    auto nodes = hash_ring_.get_all_nodes();
    Node target{};
    for (const auto& n : nodes) {
        if (n.id == conn->node_id) {
            target = n;
            break;
        }
    }
    if (target.id.empty()) return false;

    conn->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->fd < 0) return false;

    int opt = 1;
    setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));

    sockaddr_in addr = create_sockaddr(target.host, target.port);
    if (addr.sin_addr.s_addr == 0) {
        close(conn->fd);
        conn->fd = -1;
        return false;
    }

    struct timeval tv{5, 0};
    setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(conn->fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(conn->fd);
        conn->fd = -1;
        return false;
    }
    return true;
}

bool Router::read_frame(int fd, std::string& payload) {
    uint32_t len;
    ssize_t n = read(fd, &len, sizeof(len));
    if (n <= 0) return false;
    if (n != sizeof(len)) return false;

    len = __builtin_bswap32(len);
    if (len > 16 * 1024 * 1024) return false;

    payload.resize(len);
    size_t total = 0;
    while (total < len) {
        n = read(fd, &payload[total], len - total);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool Router::write_frame(int fd, const std::string& payload) {
    uint32_t len = __builtin_bswap32(static_cast<uint32_t>(payload.size()));
    if (write(fd, &len, sizeof(len)) != sizeof(len)) return false;
    if (write(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) return false;
    return true;
}

void Router::handle_client(int client_fd) {
    while (running_) {
        std::string request;
        if (!read_frame(client_fd, request)) break;

        json req;
        try {
            req = json::parse(request);
        } catch (...) {
            std::string err = R"({"status":"error","code":"INVALID_ARG","message":"Invalid JSON"})";
            write_frame(client_fd, err);
            continue;
        }

        std::string key;
        if (req.value("op", "") == "insert") {
            key = req.value("id", "");
        } else if (req.value("op", "") == "search") {
            key = req.value("embedding", json::array())[0].dump();
        } else if (req.value("op", "") == "delete") {
            auto ids = req.value("ids", json::array());
            if (!ids.empty()) key = ids[0];
        } else {
            key = "health";
        }

        auto targets = hash_ring_.get_nodes(key, 2);
        if (targets.empty()) {
            std::string err = R"({"status":"error","code":"NO_NODES","message":"No healthy nodes"})";
            write_frame(client_fd, err);
            continue;
        }

        std::string response;
        bool success = false;
        for (const auto& target : targets) {
            auto conn = acquire_connection(target);
            if (!conn) continue;

            if (write_frame(conn->fd, request)) {
                if (read_frame(conn->fd, response)) {
                    success = true;
                    release_connection(conn);
                    break;
                }
            }
            release_connection(conn);
        }

        if (!success) {
            response = R"({"status":"error","code":"UPSTREAM_FAILED","message":"All upstreams failed"})";
        }
        write_frame(client_fd, response);
    }
    close(client_fd);
}

std::string Router::resolve_hostname(const std::string& hostname) {
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    int result = getaddrinfo(hostname.c_str(), nullptr, &hints, &res);
    if (result != 0 || !res) {
        return "";
    }

    char ip[INET_ADDRSTRLEN];
    const struct sockaddr_in* addr = reinterpret_cast<const struct sockaddr_in*>(res->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ip, INET_ADDRSTRLEN);
    freeaddrinfo(res);
    return std::string(ip);
}

struct sockaddr_in Router::create_sockaddr(const std::string& hostname, int port) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    std::string ip = resolve_hostname(hostname);
    if (ip.empty()) {
        return addr;
    }

    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    return addr;
}