#include "hash_ring.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <unordered_set>

HashRing::HashRing(int virtual_nodes) : virtual_nodes_(virtual_nodes) {}

uint64_t HashRing::hash_key(const std::string& key) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(key.data()), key.size(), hash);
    
    // Use bytes 8-15 (second 64 bits) for better distribution
    // XOR with first 8 bytes for additional mixing
    uint64_t result = 0;
    std::memcpy(&result, hash + 8, sizeof(uint64_t));
    uint64_t first_part = 0;
    std::memcpy(&first_part, hash, sizeof(uint64_t));
    result ^= first_part + 0x9e3779b97f4a7c15ULL + (result << 6) + (result >> 2);
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
    auto vnodes_it = node_vnodes_.find(id);
    if (vnodes_it == node_vnodes_.end()) return;

    if (healthy) {
        node_failures_[id] = 0;
        for (uint64_t h : vnodes_it->second) {
            auto it = ring_.find(h);
            if (it != ring_.end()) {
                it->second.healthy = true;
                it->second.consecutive_failures = 0;
            }
        }
    } else {
        int failures = ++node_failures_[id];
        if (failures >= HASH_RING_MAX_CONSECUTIVE_FAILURES) {
            for (uint64_t h : vnodes_it->second) {
                auto it = ring_.find(h);
                if (it != ring_.end()) {
                    it->second.healthy = false;
                    it->second.consecutive_failures = failures;
                }
            }
        }
    }
}

std::vector<Node> HashRing::get_nodes(const std::string& key, int replication) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    if (ring_.empty() || replication <= 0) return result;

    uint64_t key_hash = hash_key(key);
    auto it = ring_.lower_bound(key_hash);
    if (it == ring_.end()) it = ring_.begin();

    std::unordered_set<std::string> seen;
    size_t iterations = 0;
    const size_t max_iterations = ring_.size() * 2;

    while (result.size() < static_cast<size_t>(replication) && iterations < max_iterations) {
        if (it == ring_.end()) it = ring_.begin();

        const Node& node = it->second;
        if (node.healthy && seen.insert(node.id).second) {
            result.push_back(node);
        }
        ++it;
        ++iterations;
    }

    return result;
}

std::vector<Node> HashRing::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    std::unordered_set<std::string> seen;
    for (const auto& [hash, node] : ring_) {
        if (seen.insert(node.id).second) {
            result.push_back(node);
        }
    }
    return result;
}