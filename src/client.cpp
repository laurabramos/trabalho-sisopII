// client.cpp - VERSÃO CORRIGIDA

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
#include "nodo.h"
#include <limits>
#include <string>

#define MAX_ATTEMPTS 5
#define TIMEOUT 2
#define MAX_SEND_ATTEMPTS 3

using namespace std;

// Construtor não precisa mais gerenciar o socket de broadcast
Client::Client(int Discovery_Port) {
    // Vazio
}

// Destrutor também não precisa mais gerenciar
Client::~Client() {
    // Vazio
}

std::string Client::discoverServer(int Discovery_Port, int Request_Port) {
    int discoverySocket = createSocket(0);

    if (discoverySocket == -1) {
        perror("Erro ao criar socket de descoberta");
        return "";
    }
    setSocketBroadcastOptions(discoverySocket);
    
    Message message = {Type::DESC, 0, 0}; // Num e seq podem ser 0 para descoberta
    int attempts = 0;

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(Discovery_Port);
    broadcastAddr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    
    cout << "Procurando servidor na rede..." << endl;

    while (attempts < MAX_ATTEMPTS) {
        sendto(discoverySocket, &message, sizeof(message), 0,
               (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;
        FD_ZERO(&read_fds);
        FD_SET(discoverySocket, &read_fds);

        int selectResult = select(discoverySocket + 1, &read_fds, NULL, NULL, &timeout);
        
        if (selectResult > 0 && FD_ISSET(discoverySocket, &read_fds)) {
            Message recMessage;
            sockaddr_in fromAddr{};
            socklen_t fromLen = sizeof(fromAddr);
            int received = recvfrom(discoverySocket, &recMessage, sizeof(Message), 0,
                                    (struct sockaddr*)&fromAddr, &fromLen);

            if (received > 0 && recMessage.type == Type::DESC_ACK) {
                std::string serverIP = inet_ntoa(fromAddr.sin_addr);
                cout << "Servidor encontrado em: " << serverIP << endl;
                
                close(discoverySocket); // Fecha o socket temporário
                return serverIP; 
            }
        }
        
        cout << "Tentativa " << attempts + 1 << ": Nenhuma resposta do servidor.\n";
        attempts++;
    }

    cout << "Limite de tentativas atingido. Não foi possível encontrar o servidor.\n";
    close(discoverySocket); // Fecha o socket temporário em caso de falha
    return ""; 
}

bool Client::sendNum(const std::string& serverIP, int Request_Port) {
    int clientSocketUni = createSocket(0); // Também não precisa de bind
    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return false;
    }
    setSocketTimeout(clientSocketUni, 3);

    sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Request_Port);
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "ERROR invalid address/ Address not supported." << endl;
        close(clientSocketUni);
        return false;
    }

    uint32_t num;
    // uint32_t soma = 0;
    // uint32_t seq = 1;

    cout << "Conectado ao servidor " << serverIP << ". Pode começar a enviar números." << endl;
    cout << "(Use Ctrl+D para finalizar)" << endl;

    while (true) {
        if (!(std::cin >> num)) {
            if (std::cin.eof()) {
                break;
            } else {
                cerr << "Entrada inválida. Tente novamente.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }
        }

        bool confirmed = false;
        int send_attempts = 0;

        while (!confirmed) {
            if (send_attempts >= MAX_SEND_ATTEMPTS) {
                cout << "Servidor " << serverIP << " não está respondendo. Falha na comunicação." << endl;
                close(clientSocketUni);
                return false;
            }

            Message message = {Type::REQ, num, this->current_seq, 0, 0, 0};

            sendto(clientSocketUni, &message, sizeof(Message), 0,
                   (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            Message response;
            socklen_t serverLen = sizeof(serverAddr);

            int received = recvfrom(clientSocketUni, &response, sizeof(Message), 0,
                                    (struct sockaddr*)&serverAddr, &serverLen);

            if (received > 0 && response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                time_t now = time(0);
                struct tm *ltm = localtime(&now);
                char buffer[21];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);
                cout << buffer << "server " << serverIP
                     << " id_req " << response.seq
                     << " value " << num
                     << " num_reqs " << response.seq
                     << " total_sum " << response.total_sum << endl;

                this->current_seq++; // Incrementa o número de sequência da classe
                confirmed = true;
            } else {
                cout << "Erro na confirmação do servidor. Reenviando requisição " << this->current_seq << " (tentativa " << send_attempts + 1 << ")...\n";
                send_attempts++;
            }
        }
    }

    close(clientSocketUni);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }

    int Discovery_Port = atoi(argv[1]);
    int Request_Port = Discovery_Port + 1;

    Client client(Discovery_Port);

    while (true) {
        std::string serverIP = client.discoverServer(Discovery_Port, Request_Port);

        if (!serverIP.empty()) {
            bool success = client.sendNum(serverIP, Request_Port);
            if (success) {
                cout << "Finalizando cliente." << endl;
                break;
            }
        } else {
            cout << "Não foi possível encontrar um servidor. Tentando novamente em 5 segundos..." << endl;
            sleep(5);
        }
    }

    return 0;
}