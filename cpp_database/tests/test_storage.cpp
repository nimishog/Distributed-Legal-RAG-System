#include "postgres_storage.hpp"
#include "storage.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cstdlib>

using json = nlohmann::json;

class PostgresStorageTest : public ::testing::Test {
protected:
    static std::unique_ptr<PostgresStorage> storage;

    static void SetUpTestSuite() {
        const char* db_host = std::getenv("TEST_DB_HOST");
        const char* db_port = std::getenv("TEST_DB_PORT");
        const char* db_name = std::getenv("TEST_DB_NAME");
        const char* db_user = std::getenv("TEST_DB_USER");
        const char* db_pass = std::getenv("TEST_DB_PASSWORD");

        if (!db_host || !db_port || !db_name || !db_user || !db_pass) {
            GTEST_SKIP() << "Test database not configured. Set TEST_DB_* env vars to run.";
        }

        std::string conn = "host=" + std::string(db_host) +
                           " port=" + std::string(db_port) +
                           " dbname=" + std::string(db_name) +
                           " user=" + std::string(db_user) +
                           " password=" + std::string(db_pass);

        storage = std::make_unique<PostgresStorage>(conn, 2);

        auto health = storage->health();
        if (!health.healthy) {
            GTEST_SKIP() << "Database not healthy: " << health.message;
        }
    }

    void SetUp() override {
        storage->remove({"test-id-1", "test-id-2", "test-id-3"});
    }

    void TearDown() override {
        storage->remove({"test-id-1", "test-id-2", "test-id-3"});
    }
};

std::unique_ptr<PostgresStorage> PostgresStorageTest::storage = nullptr;

TEST_F(PostgresStorageTest, InsertAndSearch) {
    std::vector<float> embedding(768, 0.0f);
    embedding[0] = 1.0f;

    storage->insert("test-id-1", "Test legal text about contracts", embedding, {{"source", "test"}});

    auto results = storage->search(embedding, 5);
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].id, "test-id-1");
    EXPECT_EQ(results[0].text, "Test legal text about contracts");
    EXPECT_FLOAT_EQ(results[0].score, 1.0f);
    EXPECT_EQ(results[0].metadata["source"], "test");
}

TEST_F(PostgresStorageTest, SearchTopK) {
    for (int i = 0; i < 10; ++i) {
        std::vector<float> emb(768, 0.0f);
        emb[i % 768] = 1.0f;
        storage->insert("test-id-" + std::to_string(i), "Text " + std::to_string(i), emb, {});
    }

    std::vector<float> query(768, 0.0f);
    query[0] = 1.0f;
    auto results = storage->search(query, 3);
    EXPECT_EQ(results.size(), 3);
}

TEST_F(PostgresStorageTest, Delete) {
    std::vector<float> embedding(768, 0.0f);
    embedding[0] = 1.0f;

    storage->insert("test-del-1", "To be deleted", embedding, {});
    auto results = storage->search(embedding, 5);
    EXPECT_EQ(results.size(), 1);

    storage->remove({"test-del-1"});
    results = storage->search(embedding, 5);
    EXPECT_EQ(results.size(), 0);
}

TEST_F(PostgresStorageTest, HealthCheck) {
    auto health = storage->health();
    EXPECT_TRUE(health.healthy);
    EXPECT_GE(health.total_chunks, 0);
    EXPECT_LT(health.latency.count(), 1000);
}

TEST_F(PostgresStorageTest, MetadataFilter) {
    std::vector<float> emb(768, 0.0f);
    emb[0] = 1.0f;

    storage->insert("meta-1", "Contract law", emb, {{"category", "contracts"}});
    storage->insert("meta-2", "Tort law", emb, {{"category", "torts"}});

    auto results = storage->search(emb, 10, {{"category", "contracts"}});
    for (const auto& r : results) {
        EXPECT_EQ(r.metadata["category"], "contracts");
    }
}