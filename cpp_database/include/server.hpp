#ifndef SERVER_HPP
#define SERVER_HPP

#include "storage.hpp"
#include "thread_pool.hpp"
#include <string>
#include <cstdint>
#include <functional>

class Server {
public:
    explicit Server(int port, StoragePtr storage);
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
    ThreadPool pool_;
    StoragePtr storage_;
    bool running_;
};

#endif