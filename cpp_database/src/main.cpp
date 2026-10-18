#include "server.hpp"
#include "postgres_storage.hpp"
#include <iostream>
#include <cstdlib>
#include <csignal>
#include <string>
#include <fstream>
#include <filesystem>

static Server* g_server = nullptr;

static void handle_signal(int) {
    if (g_server) {
        g_server->stop();
    }
}

std::string get_env_or_secret(const char* name, const std::string& default_value = "") {
    const char* val = std::getenv(name);
    if (val && val[0] != '\0') {
        return val;
    }
    std::string secret_path = "/run/secrets/" + std::string(name);
    if (std::filesystem::exists(secret_path)) {
        std::ifstream secret_file(secret_path);
        std::string secret;
        std::getline(secret_file, secret);
        if (!secret.empty()) {
            return secret;
        }
    }
    return default_value;
}

int main() {
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    std::string db_host = get_env_or_secret("DB_HOST", "localhost");
    std::string db_port = get_env_or_secret("DB_PORT", "5432");
    std::string db_name = get_env_or_secret("DB_NAME", "legal_rag");
    std::string db_user = get_env_or_secret("DB_USER", "rag_user");
    std::string db_password = get_env_or_secret("DB_PASSWORD", "localdev");
    std::string shard_id_str = get_env_or_secret("DB_SHARD_ID", "0");
    std::string port_str = get_env_or_secret("SERVER_PORT", "8081");
    std::string worker_threads_str = get_env_or_secret("DB_WORKER_THREADS", "4");
    std::string max_queue_str = get_env_or_secret("DB_MAX_QUEUE", "1000");

    int shard_id = std::stoi(shard_id_str);
    int port = std::stoi(port_str);
    size_t worker_threads = std::stoul(worker_threads_str);
    size_t max_queue_size = std::stoul(max_queue_str);

    std::string conn_string = "host=" + db_host +
                              " port=" + db_port +
                              " dbname=" + db_name +
                              " user=" + db_user +
                              " password=" + db_password;

    std::cout << "Starting database shard " << shard_id << " on port " << port << std::endl;
    std::cout << "Worker threads: " << worker_threads << ", Max queue: " << max_queue_size << std::endl;
    std::cout << "Connecting to PostgreSQL at " << db_host << ":" << db_port << std::endl;

    try {
        auto storage = std::make_unique<PostgresStorage>(conn_string, 4);
        auto health = storage->health();
        if (!health.healthy) {
            std::cerr << "Database health check failed: " << health.message << std::endl;
            return 1;
        }
        std::cout << "Database connected. Total chunks: " << health.total_chunks << std::endl;

        Server server(port, std::move(storage), worker_threads, max_queue_size);
        g_server = &server;
        if (!server.start()) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}