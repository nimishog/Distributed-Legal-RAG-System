#include "server.hpp"
#include "vector_math.hpp"
#include "storage.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace {

// Read exactly n bytes, looping over short reads.
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

// Validate the canonical 8-4-4-4-12 UUID form. The `legal_chunks.id` column
// is a PostgreSQL UUID, so malformed ids are rejected early with INVALID_ARG
// instead of surfacing as an opaque INTERNAL error.
bool is_valid_uuid(const std::string& id) {
    if (id.size() != 36) return false;
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

}  // namespace

Server::Server(int port, StoragePtr storage, size_t worker_threads, size_t max_queue_size)
    : port_(port), server_fd_(-1), storage_(std::move(storage)), pool_(worker_threads, max_queue_size), running_(false) {}

Server::~Server() {
    stop();
}

bool Server::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port_ << std::endl;
        return false;
    }

    if (listen(server_fd_, 10) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        return false;
    }

    running_ = true;
    std::cout << "Database server listening on port " << port_ << std::endl;
    run_accept_loop();
    return true;
}

void Server::stop() {
    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // Wait for thread pool to finish processing (with timeout)
    auto deadline = std::chrono::steady_clock::now() + shutdown_timeout_;
    while (pool_.queue_size() > 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "Shutdown timeout reached, forcing stop" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Server::run_accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                std::cerr << "Accept error" << std::endl;
            }
            continue;
        }

        if (!pool_.enqueue([this, client_fd]() {
                handle_client(client_fd);
            })) {
            // Queue full: reject the connection and always close the fd so it
            // is not leaked.
            std::string err = R"({"status":"error","code":"TOO_MANY_REQUESTS","message":"Server busy"})";
            write_frame(client_fd, err);
            close(client_fd);
        }
    }
}

void Server::handle_client(int client_fd) {
    // Bound how long an idle client may hold a worker thread. Persistent
    // connections are still supported; the router reconnects transparently.
    struct timeval tv{30, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running_) {
        std::string request;
        if (!read_frame(client_fd, request)) {
            break;
        }

        std::string response;
        process_request(request, response);

        if (!write_frame(client_fd, response)) {
            break;
        }
    }
    close(client_fd);
}

bool Server::read_frame(int fd, std::string& payload) {
    uint32_t len = 0;
    if (!read_all(fd, &len, sizeof(len))) return false;

    len = __builtin_bswap32(len);
    if (len > 16 * 1024 * 1024) {
        return false;
    }

    payload.resize(len);
    if (len > 0 && !read_all(fd, &payload[0], len)) return false;
    return true;
}

bool Server::write_frame(int fd, const std::string& payload) {
    uint32_t len = __builtin_bswap32(static_cast<uint32_t>(payload.size()));
    if (!write_all(fd, &len, sizeof(len))) return false;
    if (!payload.empty() && !write_all(fd, payload.data(), payload.size())) return false;
    return true;
}

void Server::process_request(const std::string& request, std::string& response) {
    try {
        json req = json::parse(request);
        std::string op = req.value("op", "");

        if (op == "insert") {
            std::string id = req.value("id", "");
            if (!is_valid_uuid(id)) {
                response = R"({"status":"error","code":"INVALID_ARG","message":"id must be a valid UUID"})";
                return;
            }
            std::string text = req.value("text", "");
            std::vector<float> embedding = req.value("embedding", std::vector<float>{});
            json metadata = req.value("metadata", json::object());

            storage_->insert(id, text, embedding, metadata);
            response = R"({"status":"ok"})";

        } else if (op == "batch_insert") {
            auto chunks = req.value("chunks", json::array());
            std::vector<InsertRequest> insert_requests;
            insert_requests.reserve(chunks.size());

            for (const auto& chunk : chunks) {
                InsertRequest ir;
                ir.id = chunk.value("id", "");
                if (!is_valid_uuid(ir.id)) {
                    response = R"({"status":"error","code":"INVALID_ARG","message":"every chunk id must be a valid UUID"})";
                    return;
                }
                ir.text = chunk.value("text", "");
                ir.embedding = chunk.value("embedding", std::vector<float>{});
                ir.metadata = chunk.value("metadata", json::object());
                insert_requests.push_back(std::move(ir));
            }

            storage_->batch_insert(insert_requests);
            response = R"({"status":"ok"})";

        } else if (op == "search") {
            std::vector<float> embedding = req["embedding"].get<std::vector<float>>();
            size_t top_k = req.value("top_k", 10);
            json filter = req.value("filter", json::object());

            auto results = storage_->search(embedding, top_k, filter);

            json resp;
            resp["status"] = "ok";
            json results_arr = json::array();
            for (const auto& r : results) {
                json item;
                item["id"] = r.id;
                item["text"] = r.text;
                item["score"] = r.score;
                item["metadata"] = r.metadata;
                results_arr.push_back(std::move(item));
            }
            resp["results"] = std::move(results_arr);
            response = resp.dump();

        } else if (op == "delete") {
            std::vector<std::string> ids = req.value("ids", std::vector<std::string>{});
            for (const auto& id : ids) {
                if (!is_valid_uuid(id)) {
                    response = R"({"status":"error","code":"INVALID_ARG","message":"every id must be a valid UUID"})";
                    return;
                }
            }
            storage_->remove(ids);
            response = R"({"status":"ok"})";

        } else if (op == "health") {
            auto health = storage_->health();
            json resp;
            resp["status"] = health.healthy ? "ok" : "error";
            resp["healthy"] = health.healthy;
            resp["message"] = health.message;
            resp["total_chunks"] = health.total_chunks;
            resp["latency_ms"] = health.latency.count();
            response = resp.dump();

        } else {
            response = R"({"status":"error","code":"INVALID_ARG","message":"Unknown operation"})";
        }

    } catch (const std::exception& e) {
        json resp;
        resp["status"] = "error";
        resp["code"] = "INTERNAL";
        resp["message"] = e.what();
        response = resp.dump();
    }
}