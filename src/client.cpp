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


void log_with_timestamp(const string &message) {
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}


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

    //struct sockaddr_in bcastAddr;
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
    string currentServerIP = serverIP_param;

    int clientSocketUni = createSocket(0);
    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return false;
    }
    setSocketTimeout(clientSocketUni, 3);

    // --- SEÇÃO 1: PROCESSAR A FILA DE NÚMEROS PENDENTES PRIMEIRO ---
    while (!this->unacked_nums.empty()) {
        uint32_t num_to_resend = this->unacked_nums.front();
        cout << "Reenviando número pendente " << num_to_resend << " para o novo servidor " << currentServerIP << "..." << endl;
        
        bool confirmed = false;
        int send_attempts = 0;
        while (!confirmed) {
            if (send_attempts >= MAX_SEND_ATTEMPTS) {
                cout << "O novo servidor " << currentServerIP << " também não está respondendo. Falha na comunicação." << endl;
                close(clientSocketUni);
                return false;
            }

            sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(Request_Port);
            inet_pton(AF_INET, currentServerIP.c_str(), &serverAddr.sin_addr);

            Message message = {Type::REQ, num_to_resend, this->current_seq};
            sendto(clientSocketUni, &message, sizeof(message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            Message response;
            if (recvfrom(clientSocketUni, &response, sizeof(response), 0, NULL, NULL) > 0) {
                if (response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                    log_with_timestamp("Reenvio do número " + to_string(num_to_resend) + " confirmado.");
                    this->current_seq++;
                    this->unacked_nums.pop();
                    confirmed = true;
                } else if (response.type == Type::NOT_LEADER) {
                    struct in_addr new_leader_addr = { .s_addr = response.ip_addr };
                    string newLeaderIP = inet_ntoa(new_leader_addr);
                    cout << "Redirecionado durante reenvio. Novo líder: " << newLeaderIP << endl;
                    currentServerIP = newLeaderIP;
                    send_attempts = 0;
                }
            } else {
                send_attempts++;
            }
        }
    }

    // --- SEÇÃO 2: PROCESSAR NOVOS NÚMEROS DA ENTRADA PADRÃO ---
    uint32_t num;
    cout << "Conectado ao líder " << currentServerIP << ". Digite os números (Ctrl+D para encerrar):" << endl;
    while (std::cin >> num) {
        bool confirmed = false;
        int send_attempts = 0;
        while (!confirmed) {
            if (send_attempts >= MAX_SEND_ATTEMPTS) {
                cout << "Servidor " << currentServerIP << " não está respondendo. Guardando o número " << num << " para reenviar." << endl;
                this->unacked_nums.push(num);
                close(clientSocketUni);
                return false;
            }

            sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(Request_Port);
            inet_pton(AF_INET, currentServerIP.c_str(), &serverAddr.sin_addr);

            Message message = {Type::REQ, num, this->current_seq};
            sendto(clientSocketUni, &message, sizeof(message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            Message response;
            if (recvfrom(clientSocketUni, &response, sizeof(response), 0, NULL, NULL) > 0) {
                if (response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                    log_with_timestamp("Soma no servidor: " + to_string(response.total_sum));
                    this->current_seq++;
                    confirmed = true;
                } else if (response.type == Type::NOT_LEADER) {
                    struct in_addr new_leader_addr = { .s_addr = response.ip_addr };
                    string newLeaderIP = inet_ntoa(new_leader_addr);
                    cout << "Líder mudou. Redirecionando para: " << newLeaderIP << endl;
                    currentServerIP = newLeaderIP;
                    send_attempts = 0;
                }
            } else {
                cout << "Timeout esperando confirmação. Tentativa " << send_attempts + 1 << "..." << endl;
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
