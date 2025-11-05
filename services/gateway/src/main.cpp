#include <cppkafka/cppkafka.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include "generated/score_update.pb.h"

constexpr int PORT = 7000;

int main() {

    // Create a TCP socket
    int server = socket(AF_INET, SOCK_STREAM, 0);

    // Bind to any local address and the specified port
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    bind(server, (sockaddr*)&server_addr, sizeof(server_addr));

    // Start listening for incoming connections
    listen(server, 1);
    std::cout << "Gateway listening on port " << PORT << std::endl; 
    while(1) {
        // Wait for one client to connect
        int client = accept(server, nullptr, nullptr);
        std::cout << "Client connected" << std::endl;    
        
        // Echo everything received to console
        char buffer[1024];
        ssize_t bytes_received;
        while ((bytes_received = recv(client, buffer, sizeof(buffer), 0)) > 0) {
            std::cout.write(buffer, bytes_received);
        }

        close(client);
    }
    
    // Clean up
    close(server); 
    return 0;
}