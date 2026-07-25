#include "server.hpp"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

using namespace std;

EchoServer::EchoServer(int p) : port(p), serverFd(-1) {}

EchoServer::~EchoServer() {
    stop();
}

void EchoServer::loadData() {
    // Logic to load necessary server data/configurations
    cout << "Loading server configurations and data..." << endl;
    cout << "Data loaded successfully." << endl;
}

bool EchoServer::setupSocket() {
    // 1. Create a TCP/IP socket
    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        cerr << "Error: Failed to create socket." << endl;
        return false;
    }

    // 2. Set socket options to reuse the address
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        cerr << "Error: Failed to set socket options." << endl;
        return false;
    }

    // 3. Configure the server address structure
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; 
    serverAddr.sin_port = htons(port);

    // 4. Bind the socket to the port
    if (bind(serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Error: Failed to bind to port " << port << endl;
        return false;
    }

    // 5. Ready the socket to listen for incoming connections
    if (listen(serverFd, 5) < 0) {
        cerr << "Error: Failed to listen on socket." << endl;
        return false;
    }

    return true;
}

int EchoServer::getServerFd() const {
    return serverFd;
}

void EchoServer::handleClient(int clientSocket) {
    char buffer[1024];
    
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        
        ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);

        if (bytesRead > 0) {
            cout << "Received: " << buffer;
            send(clientSocket, buffer, bytesRead, 0); // Echo
        } else if (bytesRead == 0) {
            cout << "Client disconnected." << endl;
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