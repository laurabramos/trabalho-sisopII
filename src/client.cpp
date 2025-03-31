#include "client.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>    // Para fcntl() e O_NONBLOCK
#include <sys/select.h>  // Para select()


#define MAX_ATTEMPTS 5
#define TIMEOUT 2

Client::Client() {
    clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket == -1) {
        perror("Erro ao criar socket UDP");
        exit(1);
    }

    int broadcastEnable = 1;
    setsockopt(clientSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);

    // Tornar o socket não bloqueante
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags == -1) {
        perror("Erro ao obter flags do socket");
        close(clientSocket);
        exit(1);
    }
    fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);  // Configura para não bloqueante
}

Client::~Client() {
    close(clientSocket);
}

void Client::discoverServer() {
    const char* message = "DISCOVERY";
    int attempts = 0;
    
    while (attempts < MAX_ATTEMPTS) {
        // Envia mensagem de descoberta
        sendto(clientSocket, message, strlen(message), 0, 
            (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        
        std::cout << "Mandando mensagem de descoberta...\n";

        char buffer[BUFFER_SIZE];
        socklen_t serverLen = sizeof(broadcastAddr);
        int received = -1;
        
        // Usar select para verificar se há dados para ler no socket
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT;    // Tempo máximo de espera
        timeout.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(clientSocket, &read_fds);

        int selectResult = select(clientSocket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (selectResult > 0 && FD_ISSET(clientSocket, &read_fds)) {
            // Se há dados disponíveis, tenta ler
            received = recvfrom(clientSocket, buffer, BUFFER_SIZE, 0, 
                                (struct sockaddr*)&broadcastAddr, &serverLen);
        }

        if (received > 0) {
            buffer[received] = '\0';
            std::cout << "Servidor encontrado no IP: " << buffer << std::endl;
            break;  // Sai do loop se o servidor for encontrado
        } else if (received == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Não há dados disponíveis no momento, continua tentando
                std::cout << "Nenhuma resposta do servidor. Tentando novamente...\n";
                attempts++;
            } 
        }
    }
    
    if (attempts >= MAX_ATTEMPTS) {
        std::cout << "Limite de tentativas atingido. Não foi possível encontrar o servidor.\n";
    }
}

