#include "server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

Server::Server() {
    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1) {
        perror("Erro ao criar socket UDP");
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(DISCOVERY_PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Erro ao bindar socket UDP");
        exit(1);
    }
}

Server::~Server() {
    close(serverSocket);
}

void Server::startListening() {
    char buffer[BUFFER_SIZE];
    socklen_t clientLen = sizeof(clientAddr);

    std::cout << "Servidor esperando mensagens de descoberta...\n";
    
    while (true) {
        int received = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, 
                               (struct sockaddr*)&clientAddr, &clientLen);
        
        if (received > 0) {
            buffer[received] = '\0';
            handleDiscovery(buffer, clientAddr);
        }
    }
}

void Server::handleDiscovery(char* buffer, struct sockaddr_in &clientAddr) {
    if (strcmp(buffer, "DISCOVERY") == 0) {
        std::string serverIP = getIP();
        sendto(serverSocket, serverIP.c_str(), serverIP.size(), 0, 
               (struct sockaddr*)&clientAddr, sizeof(clientAddr));
        
        std::string clientIP = inet_ntoa(clientAddr.sin_addr);
        participants.push_back(clientIP);
        std::cout << "Novo cliente registrado: " << clientIP << std::endl;
    }
}


