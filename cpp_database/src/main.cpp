#include "server.hpp"
#include "postgres_storage.hpp"
#include <iostream>
#include <cstdlib>
#include <string>

std::string get_env(const char* name, const std::string& default_value = "") {
    const char* val = std::getenv(name);
    return val ? val : default_value;
}

int main() {
    std::string db_host = get_env("DB_HOST", "localhost");
    std::string db_port = get_env("DB_PORT", "5432");
    std::string db_name = get_env("DB_NAME", "legal_rag");
    std::string db_user = get_env("DB_USER", "rag_user");
    std::string db_password = get_env("DB_PASSWORD", "localdev");
    std::string shard_id_str = get_env("DB_SHARD_ID", "0");
    std::string port_str = get_env("SERVER_PORT", "8081");

    int shard_id = std::stoi(shard_id_str);
    int port = std::stoi(port_str);

    std::string conn_string = "host=" + db_host +
                              " port=" + db_port +
                              " dbname=" + db_name +
                              " user=" + db_user +
                              " password=" + db_password;

    std::cout << "Starting database shard " << shard_id << " on port " << port << std::endl;
    std::cout << "Connecting to PostgreSQL at " << db_host << ":" << db_port << std::endl;

    try {
        auto storage = std::make_unique<PostgresStorage>(conn_string, 4);
        auto health = storage->health();
        if (!health.healthy) {
            std::cerr << "Database health check failed: " << health.message << std::endl;
            return 1;
        }
        std::cout << "Database connected. Total chunks: " << health.total_chunks << std::endl;

        Server server(port, std::move(storage));
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