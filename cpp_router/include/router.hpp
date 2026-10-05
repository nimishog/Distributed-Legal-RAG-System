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

struct UpstreamConnection {
    int fd = -1;
    std::string node_id;
    std::chrono::steady_clock::time_point last_used;
    bool in_use = false;
};

class Router {
public:
    Router(int port, const std::vector<Node>& nodes);
    ~Router();

    void start();
    void stop();

private:
    void run_accept_loop();
    void health_check_loop();
    void handle_client(int client_fd);
    bool read_frame(int fd, std::string& payload);
    bool write_frame(int fd, const std::string& payload);

    UpstreamConnection* acquire_connection(const Node& node);
    void release_connection(UpstreamConnection* conn);
    bool ensure_connected(UpstreamConnection* conn);
    std::string forward_request(const std::string& request, const std::vector<Node>& targets);

    std::string resolve_hostname(const std::string& hostname);
    struct sockaddr_in create_sockaddr(const std::string& hostname, int port);

    int port_;
    int server_fd_;
    HashRing hash_ring_;
    std::atomic<bool> running_;

    std::mutex pool_mutex_;
    std::unordered_map<std::string, std::vector<std::unique_ptr<UpstreamConnection>>> connection_pools_;
    const size_t pool_size_per_node_ = 8;

    std::thread health_thread_;
    const std::chrono::seconds health_interval_{10};
};

#endif