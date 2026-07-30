#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include "thread_pool.hpp" // Bring in the thread pool

using namespace std;

class EchoServer {
private:
    int port;
    int serverFd;
    ThreadPool pool; // Thread pool instance

public:
    EchoServer(int port);
    ~EchoServer();

    void loadData();
    bool setupSocket();
    int getServerFd() const;
    
    // New method to pass the socket to the background workers
    void enqueueClient(int clientSocket);
    void handleClient(int clientSocket);
    
    void stop();
};

#endif