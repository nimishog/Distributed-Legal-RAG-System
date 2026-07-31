#include "server.hpp"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

using namespace std;

// Initialize the pool with 4 worker threads
EchoServer::EchoServer(int p) : port(p), serverFd(-1), pool(4) {}

EchoServer::~EchoServer() {
    stop();
}

void EchoServer::loadData() {
    cout << "Loading server configurations and data..." << endl;
    cout << "Data loaded successfully." << endl;
}

bool EchoServer::setupSocket() {
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        cerr << "Error: Failed to create socket." << endl;
        return false;
    }

    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        cerr << "Error: Failed to set socket options." << endl;
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; 
    serverAddr.sin_port = htons(port);

    if (bind(serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error: Failed to bind to port " << port << endl;
        return false;
    }

    if (listen(serverFd, 5) < 0) {
        cerr << "Error: Failed to listen on socket." << endl;
        return false;
    }

    return true;
}

int EchoServer::getServerFd() const {
    return serverFd;
}

// Wraps the handleClient function in a lambda and pushes it to the thread queue
void EchoServer::enqueueClient(int clientSocket) {
    pool.enqueue([this, clientSocket]() {
        this->handleClient(clientSocket);
    });
}

void EchoServer::handleClient(int clientSocket) {
    char buffer[1024];
    
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        
        ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);

        if (bytesRead > 0) {
            // Printed the Thread ID to prove concurrency is working
            cout << "[Thread " << this_thread::get_id() << "] Received: " << buffer;
            send(clientSocket, buffer, bytesRead, 0);
        } else if (bytesRead == 0) {
            cout << "Client disconnected from [Thread " << this_thread::get_id() << "]" << endl;
            break; 
        } else {
            cerr << "Error reading from client." << endl;
            break;
        }
    }
    
    close(clientSocket);
}

void EchoServer::stop() {
    if (serverFd != -1) {
        close(serverFd);
        serverFd = -1;
    }
}