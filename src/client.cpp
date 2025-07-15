#include "libs/client.h"
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
#include "libs/nodo.h"
#include <limits>
#include <string>
#include <queue>

#define MAX_ATTEMPTS 5
#define TIMEOUT 5
#define MAX_SEND_ATTEMPTS 5

using namespace std;

Client::Client(int Discovery_Port) {

}

Client::~Client() {
    
}

string Client::discoverServer(int Discovery_Port, int Request_Port) {
    int discoverySocket = createSocket(0);

    if (discoverySocket == -1) {
        perror("Erro ao criar socket de descoberta");
        return "";
    }
    setSocketBroadcastOptions(discoverySocket);
    
    Message message = {Type::DESC, 0, 0}; 
    int attempts = 0;

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(Discovery_Port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcastAddr.sin_addr);
    
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
                char buffer[21];
                time_t now = time(0);
                struct tm *ltm = localtime(&now);
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);
                cout << buffer << "server_addr " << serverIP << endl;
                
                close(discoverySocket); 
                return serverIP; 
            }
        }
        
        cout << "Tentativa " << attempts + 1 << ": Nenhuma resposta do servidor.\n";
        attempts++;
    }

    cout << "Limite de tentativas atingido. Não foi possível encontrar o servidor.\n";
    close(discoverySocket); 
    return ""; 
}

bool Client::sendNum(const std::string& serverIP_param, int Request_Port) {
    string currentServerIP = serverIP_param; // Cria uma cópia local para poder ser modificada

    int clientSocketUni = createSocket(0);
    if (clientSocketUni == -1) { /* ... */ }
    setSocketTimeout(clientSocketUni, 3);

    uint32_t num;
    while (std::cin >> num) {
        bool confirmed = false;
        int send_attempts = 0;

        while (!confirmed) {
            if (send_attempts >= MAX_SEND_ATTEMPTS) {
                cout << "Servidor " << currentServerIP << " não está respondendo. Falha na comunicação." << endl;
                close(clientSocketUni);
                return false; // Retorna false para o main forçar uma nova descoberta
            }

            // Configura o endereço do servidor atual
            sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(Request_Port);
            inet_pton(AF_INET, currentServerIP.c_str(), &serverAddr.sin_addr);

            Message message = {Type::REQ, num, this->current_seq, 0, 0, 0};
            sendto(clientSocketUni, &message, sizeof(Message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            Message response;
            socklen_t serverLen = sizeof(serverAddr);
            int received = recvfrom(clientSocketUni, &response, sizeof(Message), 0, (struct sockaddr*)&serverAddr, &serverLen);

            if (received > 0) {
                if (response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                    // SUCESSO: O servidor é o líder e respondeu
                    // ... (seu código de impressão de log) ...
                    cout << "Resposta recebida do líder " << currentServerIP << endl;
                    this->current_seq++;
                    confirmed = true;
                } 
                else if (response.type == Type::NOT_LEADER) {
                    // REDIRECIONAMENTO: O servidor informou que não é o líder
                    struct in_addr new_leader_addr;
                    new_leader_addr.s_addr = response.ip_addr;
                    string newLeaderIP = inet_ntoa(new_leader_addr);
                    
                    cout << "Redirecionado. O servidor " << currentServerIP << " não é o líder. Novo líder: " << newLeaderIP << endl;
                    currentServerIP = newLeaderIP; // ATUALIZA O IP DO LÍDER
                    send_attempts = 0; // Reseta as tentativas e tenta novamente com o novo líder
                    continue; // Volta ao início do loop while(!confirmed)
                }
            } else {
                // TIMEOUT ou ERRO
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
            if (!client.sendNum(serverIP, Request_Port)) {
                 cout << "Tentando encontrar um novo servidor..." << endl;
                 sleep(5);
            } else {
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
