#include "server.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {
    int port = 8080;
    
    EchoServer server(port);
    server.loadData();
    
    if (!server.setupSocket()) {
        cerr << "Server failed to initialize networking." << endl;
        return 1;
    }
    
    cout << "Multithreaded Echo Server listening on port " << port << "..." << endl;
    
    int serverFd = server.getServerFd();
    
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket < 0) {
            cerr << "Error: Failed to accept client connection." << endl;
            continue;
        }

        cout << "New connection accepted from " << inet_ntoa(clientAddr.sin_addr) << endl;
        
        // Hand the socket off to the Thread Pool; DO NOT block here.
        server.enqueueClient(clientSocket);
    }

    return 0;
}