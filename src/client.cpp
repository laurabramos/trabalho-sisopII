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

bool Client::sendNum(const std::string& serverIP, int Request_Port) {
    int clientSocketUni = createSocket(0);
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

    if (!this->unacked_nums.empty()) {
        cout << "Reenviando " << this->unacked_nums.size() << " número(s) pendente(s) para o novo servidor..." << endl;
        
        while(!this->unacked_nums.empty()) {
            uint32_t num_to_resend = this->unacked_nums.front();
            bool confirmed = false;
            int send_attempts = 0;

            while (!confirmed) {
                 if (send_attempts >= MAX_SEND_ATTEMPTS) {
                    cout << "O novo servidor " << serverIP << " também não está respondendo. Falha na comunicação." << endl;
                    close(clientSocketUni);
                    return false;
                }
                
                cout << "Reenviando requisição " << this->current_seq << " com o número " << num_to_resend << " (tentativa " << send_attempts + 1 << ")..." << endl;
                Message message = {Type::REQ, num_to_resend, this->current_seq, 0, 0, 0};
                sendto(clientSocketUni, &message, sizeof(Message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

                Message response;
                socklen_t serverLen = sizeof(serverAddr);
                int received = recvfrom(clientSocketUni, &response, sizeof(Message), 0, (struct sockaddr*)&serverAddr, &serverLen);

                if (received > 0 && response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                    time_t now = time(0);
                    struct tm *ltm = localtime(&now);
                    char buffer[21];
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);
                    cout << buffer << "server " << serverIP
                         << " id_req " << response.seq
                         << " value " << num_to_resend
                         << " num_reqs " << response.seq
                         << " total_sum " << response.total_sum << endl;

                    this->current_seq++;
                    confirmed = true;
                    this->unacked_nums.pop(); 
                } else {
                    send_attempts++;
                }
            }
        }
        cout << "Todos os números pendentes foram confirmados pelo novo servidor." << endl;
    }


    uint32_t num;


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
                cout << "Guardando o número " << num << " para reenviar a um novo servidor." << endl;
                this->unacked_nums.push(num);
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

                this->current_seq++;
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
