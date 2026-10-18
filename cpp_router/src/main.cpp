#include "router.hpp"
#include "hash_ring.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <csignal>
#include <atomic>
#include <thread>

std::string get_env(const char* name, const std::string& default_value = "") {
    const char* val = std::getenv(name);
    return val ? val : default_value;
}

Router* g_router = nullptr;

void signal_handler(int signum) {
    if (g_router) {
        g_router->stop();
    }
}

int main() {
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE to prevent crashes on broken pipes

    std::string port_str = get_env("ROUTER_PORT", "8080");
    std::string http_port_str = get_env("ROUTER_HTTP_PORT", "8081");
    std::string worker_threads_str = get_env("ROUTER_WORKER_THREADS", "4");
    std::string max_queue_str = get_env("ROUTER_MAX_QUEUE", "1000");
    std::string db_nodes = get_env("DB_NODES", "cpp_database_0:8081,cpp_database_1:8081,cpp_database_2:8081");

    int port = std::stoi(port_str);
    int http_port = std::stoi(http_port_str);
    size_t worker_threads = std::stoul(worker_threads_str);
    size_t max_queue = std::stoul(max_queue_str);

    std::vector<Node> nodes;
    std::stringstream ss(db_nodes);
    std::string node_str;
    int shard = 0;
    while (std::getline(ss, node_str, ',')) {
        size_t colon = node_str.find(':');
        if (colon != std::string::npos) {
            std::string host = node_str.substr(0, colon);
            int p = std::stoi(node_str.substr(colon + 1));
            nodes.push_back({"shard-" + std::to_string(shard), host, p});
            shard++;
        }
    }

    if (nodes.empty()) {
        std::cerr << "No database nodes configured" << std::endl;
        return 1;
    }

    std::cout << "Starting router on port " << port << " (binary), " << http_port << " (HTTP) with " << nodes.size() << " database nodes" << std::endl;
    std::cout << "Worker threads: " << worker_threads << ", Max request queue: " << max_queue << std::endl;
    for (const auto& n : nodes) {
        std::cout << "  - " << n.id << ": " << n.host << ":" << n.port << std::endl;
    }

    try {
        Router router(port, nodes, http_port, worker_threads, max_queue);
        g_router = &router;
        router.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}