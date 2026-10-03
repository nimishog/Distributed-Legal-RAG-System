#ifndef HASH_RING_HPP
#define HASH_RING_HPP

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <functional>

struct Node {
    std::string id;
    std::string host;
    int port;
    bool healthy = true;
    uint64_t hash = 0;
};

class HashRing {
public:
    explicit HashRing(int virtual_nodes = 50);

    void add_node(const std::string& id, const std::string& host, int port);
    void remove_node(const std::string& id);
    void mark_healthy(const std::string& id, bool healthy);

    std::vector<Node> get_nodes(const std::string& key, int replication = 2) const;
    std::vector<Node> get_all_nodes() const;

private:
    uint64_t hash_key(const std::string& key) const;
    uint64_t hash_vnode(const std::string& node_id, int vnode_idx) const;

    int virtual_nodes_;
    std::map<uint64_t, Node> ring_;
    std::map<std::string, std::vector<uint64_t>> node_vnodes_;
    mutable std::mutex mutex_;
};

#endif