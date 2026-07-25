#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace std;

class EchoServer {
private:
    int port;
    int serverFd;

public:
    EchoServer(int port);
    ~EchoServer();

    // Initializes server and loads data
    void loadData();

    // TCP/IP socket and port bindings
    bool setupSocket();
    
    // Retrieves the socket descriptor for the listener loop in main.cpp
    int getServerFd() const;
    
    // Client handling logic (Echo text back)
    void handleClient(int clientSocket);
    
    void stop();
};

#endif