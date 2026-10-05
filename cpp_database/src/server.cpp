#include "server.hpp"
#include "vector_math.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

Server::Server(int port, StoragePtr storage)
    : port_(port), server_fd_(-1), pool_(4), storage_(std::move(storage)), running_(false) {}

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

        pool_.enqueue([this, client_fd]() {
            handle_client(client_fd);
        });
    }
}

void Server::handle_client(int client_fd) {
    std::cerr << "[DEBUG] New client connected, fd=" << client_fd << std::endl;
    while (running_) {
        std::string request;
        std::cerr << "[DEBUG] Reading frame..." << std::endl;
        if (!read_frame(client_fd, request)) {
            std::cerr << "[DEBUG] read_frame returned false" << std::endl;
            break;
        }
        std::cerr << "[DEBUG] Read request: " << request << std::endl;

        std::string response;
        std::cerr << "[DEBUG] Processing request..." << std::endl;
        process_request(request, response);
        std::cerr << "[DEBUG] Processed request, response size: " << response.size() << std::endl;

        if (!write_frame(client_fd, response)) {
            std::cerr << "[DEBUG] write_frame failed" << std::endl;
            break;
        }
        std::cerr << "[DEBUG] Response sent" << std::endl;
    }
    std::cerr << "[DEBUG] Closing client connection" << std::endl;
    close(client_fd);
}

bool Server::read_frame(int fd, std::string& payload) {
    uint32_t len;
    ssize_t n = read(fd, &len, sizeof(len));
    if (n <= 0) return false;
    if (n != sizeof(len)) return false;

    len = __builtin_bswap32(len);
    if (len > 16 * 1024 * 1024) {
        std::cerr << "Frame too large: " << len << std::endl;
        return false;
    }

    payload.resize(len);
    size_t total = 0;
    while (total < len) {
        n = read(fd, &payload[total], len - total);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool Server::write_frame(int fd, const std::string& payload) {
    uint32_t len = __builtin_bswap32(static_cast<uint32_t>(payload.size()));
    if (write(fd, &len, sizeof(len)) != sizeof(len)) return false;
    if (write(fd, payload.data(), payload.size()) != static_cast<ssize_t>(payload.size())) return false;
    return true;
}

void Server::process_request(const std::string& request, std::string& response) {
    try {
        json req = json::parse(request);
        std::string op = req.value("op", "");

        if (op == "insert") {
            std::string id = req["id"];
            std::string text = req["text"];
            std::vector<float> embedding = req["embedding"].get<std::vector<float>>();
            json metadata = req.value("metadata", json::object());

            storage_->insert(id, text, embedding, metadata);
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
            std::vector<std::string> ids = req["ids"].get<std::vector<std::string>>();
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