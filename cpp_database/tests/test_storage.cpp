#include "postgres_storage.hpp"
#include "storage.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

using json = nlohmann::json;

// legal_chunks.id is a PostgreSQL UUID, so tests must use valid UUIDs.
static std::string uid(int n) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "00000000-0000-0000-0000-%012llu",
                 static_cast<unsigned long long>(n));
    return buf;
}

static const std::string ID1 = uid(1);
static const std::string ID2 = uid(2);
static const std::string ID3 = uid(3);

class PostgresStorageTest : public ::testing::Test {
protected:
    static std::unique_ptr<PostgresStorage> storage;

    static void SetUpTestSuite() {
        const char* db_host = std::getenv("TEST_DB_HOST");
        const char* db_port = std::getenv("TEST_DB_PORT");
        const char* db_name = std::getenv("TEST_DB_NAME");
        const char* db_user = std::getenv("TEST_DB_USER");
        const char* db_pass = std::getenv("TEST_DB_PASSWORD");

        // Leave storage null when the DB is unavailable; SetUp()/each test
        // body then skips. GTEST_SKIP() here does not actually skip the suite
        // in every googletest build, and a null storage would segfault.
        if (!db_host || !db_port || !db_name || !db_user || !db_pass) {
            return;
        }

        std::string conn = "host=" + std::string(db_host) +
                           " port=" + std::string(db_port) +
                           " dbname=" + std::string(db_name) +
                           " user=" + std::string(db_user) +
                           " password=" + std::string(db_pass);

        storage = std::make_unique<PostgresStorage>(conn, 2);

        auto health = storage->health();
        if (!health.healthy) {
            storage.reset();
            return;
        }
    }

    // Remove every id any test may have inserted so tests are isolated even
    // when a previous run aborted mid-test.
    static void clean() {
        std::vector<std::string> ids;
        for (int i = 1; i <= 200; ++i) ids.push_back(uid(i));
        storage->remove(ids);
    }

    void SetUp() override {
        if (!storage) {
            GTEST_SKIP() << "Test database not configured. Set TEST_DB_* env vars to run.";
        }
        clean();
    }

    void TearDown() override {
        if (storage) clean();
    }
};

std::unique_ptr<PostgresStorage> PostgresStorageTest::storage = nullptr;

TEST_F(PostgresStorageTest, InsertAndSearch) {
    if (!storage) GTEST_SKIP() << "Test database not configured.";

    std::vector<float> embedding(384, 0.0f);
    embedding[0] = 1.0f;

    storage->insert(ID1, "Test legal text about contracts", embedding, {{"source", "test"}});

    auto results = storage->search(embedding, 5);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, ID1);
    EXPECT_EQ(results[0].text, "Test legal text about contracts");
    EXPECT_FLOAT_EQ(results[0].score, 1.0f);
    EXPECT_EQ(results[0].metadata["source"], "test");
}

TEST_F(PostgresStorageTest, SearchTopK) {
    if (!storage) GTEST_SKIP() << "Test database not configured.";

    const json EMPTY = json::object();
    for (int i = 0; i < 10; ++i) {
        std::vector<float> emb(384, 0.0f);
        emb[i % 384] = 1.0f;
        storage->insert(uid(100 + i), "Text " + std::to_string(i), emb, EMPTY);
    }

    std::vector<float> query(384, 0.0f);
    query[0] = 1.0f;
    auto results = storage->search(query, 3);
    EXPECT_EQ(results.size(), 3u);
}

TEST_F(PostgresStorageTest, Delete) {
    if (!storage) GTEST_SKIP() << "Test database not configured.";

    std::vector<float> embedding(384, 0.0f);
    embedding[0] = 1.0f;

    storage->insert(ID1, "To be deleted", embedding, json::object());
    auto results = storage->search(embedding, 5);
    EXPECT_EQ(results.size(), 1u);

    storage->remove({ID1});
    results = storage->search(embedding, 5);
    EXPECT_EQ(results.size(), 0u);
}

TEST_F(PostgresStorageTest, HealthCheck) {
    if (!storage) GTEST_SKIP() << "Test database not configured.";

    auto health = storage->health();
    EXPECT_TRUE(health.healthy);
    EXPECT_GE(health.total_chunks, 0u);
    EXPECT_LT(health.latency.count(), 1000);
}

TEST_F(PostgresStorageTest, MetadataFilter) {
    if (!storage) GTEST_SKIP() << "Test database not configured.";

    std::vector<float> emb(384, 0.0f);
    emb[0] = 1.0f;

    storage->insert(uid(10), "Contract law", emb, {{"category", "contracts"}});
    storage->insert(uid(11), "Tort law", emb, {{"category", "torts"}});

    auto results = storage->search(emb, 10, {{"category", "contracts"}});
    for (const auto& r : results) {
        EXPECT_EQ(r.metadata["category"], "contracts");
    }
}
