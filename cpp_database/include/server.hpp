#ifndef SERVER_HPP
#define SERVER_HPP

#include "storage.hpp"
#include "thread_pool.hpp"
#include <string>
#include <cstdint>
#include <functional>
#include <atomic>
#include <chrono>

class Server {
public:
    Server(int port, StoragePtr storage, size_t worker_threads = 4, size_t max_queue_size = 1000);
    ~Server();

    bool start();
    void stop();

private:
    void run_accept_loop();
    void handle_client(int client_fd);
    bool read_frame(int fd, std::string& payload);
    bool write_frame(int fd, const std::string& payload);
    void process_request(const std::string& request, std::string& response);

    int port_;
    int server_fd_;
    // storage_ is declared before pool_ so that pool_ (and its worker
    // threads) is destroyed first during shutdown. This guarantees no worker
    // thread is still touching storage_ when it is destroyed.
    StoragePtr storage_;
    ThreadPool pool_;
    std::atomic<bool> running_;
    std::chrono::seconds shutdown_timeout_{30};
};

#endif