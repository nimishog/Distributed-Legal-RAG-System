#include "hash_ring.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <cstring>

HashRing::HashRing(int virtual_nodes) : virtual_nodes_(virtual_nodes) {}

uint64_t HashRing::hash_key(const std::string& key) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(key.data()), key.size(), hash);
    uint64_t result = 0;
    std::memcpy(&result, hash, sizeof(uint64_t));
    return result;
}

uint64_t HashRing::hash_vnode(const std::string& node_id, int vnode_idx) const {
    std::string vkey = node_id + "-vn-" + std::to_string(vnode_idx);
    return hash_key(vkey);
}

void HashRing::add_node(const std::string& id, const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    Node node{id, host, port, true, 0};
    node_vnodes_[id].reserve(virtual_nodes_);

    for (int i = 0; i < virtual_nodes_; ++i) {
        uint64_t h = hash_vnode(id, i);
        ring_[h] = node;
        node_vnodes_[id].push_back(h);
    }
}

void HashRing::remove_node(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = node_vnodes_.find(id);
    if (it != node_vnodes_.end()) {
        for (uint64_t h : it->second) {
            ring_.erase(h);
        }
        node_vnodes_.erase(it);
    }
}

void HashRing::mark_healthy(const std::string& id, bool healthy) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [hash, node] : ring_) {
        if (node.id == id) {
            node.healthy = healthy;
        }
    }
}

std::vector<Node> HashRing::get_nodes(const std::string& key, int replication) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    if (ring_.empty()) return result;

    uint64_t key_hash = hash_key(key);
    auto it = ring_.lower_bound(key_hash);
    if (it == ring_.end()) it = ring_.begin();

    std::string start_id;
    int count = 0;
    int iterations = 0;
    const int max_iterations = ring_.size() * 2;

    while (count < replication && iterations < max_iterations) {
        if (it == ring_.end()) it = ring_.begin();

        if (it->second.healthy) {
            if (result.empty() || it->second.id != result.back().id) {
                result.push_back(it->second);
                count++;
            }
        }
        ++it;
        iterations++;
    }

    return result;
}

std::vector<Node> HashRing::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    for (const auto& [hash, node] : ring_) {
        if (result.empty() || result.back().id != node.id) {
            result.push_back(node);
        }
    }
    return result;
}