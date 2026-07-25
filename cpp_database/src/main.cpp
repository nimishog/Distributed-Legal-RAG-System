#include "server.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {
    int port = 8080;
    
    // Entry point: Initialize the server object
    EchoServer server(port);
    
    // Load required data prior to opening the network
    server.loadData();
    
    // Setup TCP/IP bindings and state (handled by server.cpp)
    if (!server.setupSocket()) {
        cerr << "Server failed to initialize networking." << endl;
        return 1;
    }
    
    cout << "Echo server listening on port " << port << "..." << endl;
    
    int serverFd = server.getServerFd();
    
    // Network listener implementation
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        // Wait and listen for incoming client connections
        int clientSocket = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            cerr << "Error: Failed to accept client connection." << endl;
            continue;
        }

        cout << "Client connected from " << inet_ntoa(clientAddr.sin_addr) << endl;
        
        // Pass the connected socket off to the handler
        server.handleClient(clientSocket);
    }

    return 0;
}