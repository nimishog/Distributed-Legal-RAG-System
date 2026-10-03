#include "router.hpp"
#include "hash_ring.hpp"
#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>

std::string get_env(const char* name, const std::string& default_value = "") {
    const char* val = std::getenv(name);
    return val ? val : default_value;
}

int main() {
    std::string port_str = get_env("ROUTER_PORT", "8080");
    std::string db_nodes = get_env("DB_NODES", "cpp_database_0:8081,cpp_database_1:8081,cpp_database_2:8081");

    int port = std::stoi(port_str);

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

    std::cout << "Starting router on port " << port << " with " << nodes.size() << " database nodes" << std::endl;
    for (const auto& n : nodes) {
        std::cout << "  - " << n.id << ": " << n.host << ":" << n.port << std::endl;
    }

    try {
        Router router(port, nodes);
        router.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}