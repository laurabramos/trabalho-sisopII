#include "client.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <cstdint>
#include <fstream>
#include <ifaddrs.h>

#define MAX_ATTEMPTS 5
#define TIMEOUT 2

using namespace std;

// Construtor da classe Client
Client::Client() {
    // Criação do socket UDP para broadcast
    clientSocketBroad = createSocket(DISCOVERY_PORT);
    setSocketBroadcastOptions(clientSocketBroad);
}

// Destrutor
Client::~Client() {
    close(clientSocketBroad);
}

void printBytes(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        printf("%02X ", bytes[i]);
    }
    printf("\n");
}



// Descoberta de servidor por broadcast
void Client::discoverServer() {
    Message message = {Type::DESC, 42, 15};
    printBytes(&message, sizeof(message));  // Deve imprimir exatamente 9 bytes se tudo estiver certo
    std::cout << "sizeof(Message): " << sizeof(Message) << std::endl;  // Deve imprimir 9
    int attempts = 0;   

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);
    broadcastAddr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    bzero(&(broadcastAddr.sin_zero), 8);

    while (attempts < MAX_ATTEMPTS) {
        // Envia broadcast
        ssize_t sent = sendto(clientSocketBroad, &message, sizeof(message), 0,
                              (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        if (sent == -1) {
            perror("Erro no sendto (broadcast)");
        }

        std::cout << "Mandando mensagem de descoberta...\n";

        Message recMessage;
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);
        int received = -1;

        // Select pra esperar resposta
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;
        FD_ZERO(&read_fds);
        FD_SET(clientSocketBroad, &read_fds);

        int selectResult = select(clientSocketBroad + 1, &read_fds, NULL, NULL, &timeout);

        if (selectResult > 0 && FD_ISSET(clientSocketBroad, &read_fds)) {
            received = recvfrom(clientSocketBroad, &recMessage, sizeof(Message), 0,
                                (struct sockaddr*)&fromAddr, &fromLen);
        }

        if (received > 0 && recMessage.type == Type::DESC_ACK) {
            char *serverIP = inet_ntoa(fromAddr.sin_addr);
            time_t now = time(0);
            struct tm *ltm = localtime(&now);

            char buffer[20];  // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

            std::cout << buffer << serverIP << std::endl;

            sendNum(serverIP);
            break;
        } else if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            std::cout << "Nenhuma resposta do servidor. Tentando novamente...\n";
            attempts++;
        }
    }


    if (attempts >= MAX_ATTEMPTS) {
        std::cout << "Limite de tentativas atingido. Não foi possível encontrar o servidor.\n";
    }
}

// Envia números via unicast
void Client::sendNum(const char *serverIP) {
    int clientSocketUni = createSocket(RESQUEST_PORT);
    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return;
    }
    setSocketTimeout(clientSocketUni,3);

    sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(RESQUEST_PORT);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0)
    {
        std::cerr << "ERROR invalid address/ Address not supported." << std::endl;
        close(clientSocketUni);
        return;
    }

    uint32_t num;
    uint32_t seq = 1;

    while (true){
        if (std::cin >> num) {
            //std::cout << "tchaaaau. mandando para " << serverIP << std::endl;
            bool confirmed = false;
    
            while (!confirmed) {
                Message message = {Type::REQ, num, seq};
    
                if (sendto(clientSocketUni, &message, sizeof(Message), 0,
                           (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
                    perror("Erro ao enviar número");
                    close(clientSocketUni);
                    return;
                }
                //printf("skjdhflasjdfka");
    
                Message response;
                socklen_t serverLen = sizeof(serverAddr);
    
                int received = recvfrom(clientSocketUni, &response, sizeof(Message), 0,
                                        (struct sockaddr*)&serverAddr, &serverLen);
                //printf("adkfajjjjjjjjjj");
                if (received > 0 && response.seq == seq) {
                    seq++;
                    confirmed = true;
                } else {
                    std::cout << "Erro na confirmação do servidor. Reenviando requisição " << seq << "...\n";
                }
            }
        }
    }

    close(clientSocketUni);
}
