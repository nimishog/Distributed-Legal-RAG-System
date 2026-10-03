#include "hash_ring.hpp"
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <set>
#include <map>

TEST(HashRingTest, AddAndGetNodes) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    auto nodes = ring.get_nodes("test-key", 2);
    ASSERT_EQ(nodes.size(), 2);
    EXPECT_NE(nodes[0].id, nodes[1].id);
}

TEST(HashRingTest, ReplicationFactor) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    auto nodes = ring.get_nodes("test-key", 3);
    ASSERT_EQ(nodes.size(), 3);
    std::set<std::string> ids;
    for (const auto& n : nodes) ids.insert(n.id);
    EXPECT_EQ(ids.size(), 3);
}

TEST(HashRingTest, RemoveNode) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    ring.remove_node("node-2");
    auto nodes = ring.get_nodes("test-key", 3);
    EXPECT_EQ(nodes.size(), 2);
    for (const auto& n : nodes) {
        EXPECT_NE(n.id, "node-2");
    }
}

TEST(HashRingTest, MarkUnhealthy) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    ring.mark_healthy("node-2", false);
    auto nodes = ring.get_nodes("test-key", 3);
    EXPECT_EQ(nodes.size(), 2);
    for (const auto& n : nodes) {
        EXPECT_NE(n.id, "node-2");
    }
}

TEST(HashRingTest, DistributionUniformity) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    std::map<std::string, int> counts;
    for (int i = 0; i < 10000; ++i) {
        auto nodes = ring.get_nodes("key-" + std::to_string(i), 1);
        if (!nodes.empty()) {
            counts[nodes[0].id]++;
        }
    }

    for (const auto& [id, count] : counts) {
        double ratio = static_cast<double>(count) / 10000.0;
        EXPECT_NEAR(ratio, 1.0 / 3.0, 0.05) << "Node " << id << " has " << count << " hits";
    }
}

TEST(HashRingTest, ConsistentHashing) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);

    auto nodes1 = ring.get_nodes("consistent-key", 2);
    auto nodes2 = ring.get_nodes("consistent-key", 2);
    EXPECT_EQ(nodes1[0].id, nodes2[0].id);
    EXPECT_EQ(nodes1[1].id, nodes2[1].id);
}

TEST(HashRingTest, GetAllNodes) {
    HashRing ring(50);
    ring.add_node("node-1", "localhost", 8081);
    ring.add_node("node-2", "localhost", 8082);
    ring.add_node("node-3", "localhost", 8083);

    auto all = ring.get_all_nodes();
    ASSERT_EQ(all.size(), 3);
    std::set<std::string> ids;
    for (const auto& n : all) ids.insert(n.id);
    EXPECT_EQ(ids.size(), 3);
}