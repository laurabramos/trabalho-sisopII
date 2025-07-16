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



bool Client::sendOneRequest(uint32_t num, std::string& currentServerIP, int requestPort) {
    int clientSocket = createSocket(0);
    if (clientSocket == -1) return false;
    setSocketTimeout(clientSocket, 3);

    bool confirmed = false;
    int send_attempts = 0;

    while (!confirmed && send_attempts < MAX_SEND_ATTEMPTS) {
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(requestPort);
        inet_pton(AF_INET, currentServerIP.c_str(), &serverAddr.sin_addr);

        Message message = {Type::REQ, num, this->current_seq};
        sendto(clientSocket, &message, sizeof(message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

        Message response;
        if (recvfrom(clientSocket, &response, sizeof(response), 0, NULL, NULL) > 0) {
            if (response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                log_with_timestamp("Soma no servidor: " + to_string(response.total_sum));
                this->current_seq++; // O único lugar onde o seq é incrementado
                confirmed = true;
            } else if (response.type == Type::NOT_LEADER) {
                struct in_addr new_leader_addr = { .s_addr = response.ip_addr };
                currentServerIP = inet_ntoa(new_leader_addr);
                cout << "Líder mudou. Redirecionando para: " << currentServerIP << endl;
                send_attempts = 0; // Reseta as tentativas para o novo líder
            }
        } else {
            cout << "Timeout esperando confirmação. Tentativa " << send_attempts + 1 << "..." << endl;
            send_attempts++;
        }
    }

    close(clientSocket);
    return confirmed;
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Uso: " << argv[0] << " <porta_descoberta>" << endl;
        return 1;
    }

    int discovery_port = atoi(argv[1]);
    int request_port = discovery_port + 1;

    Client client(discovery_port);
    string serverIP = "";

    // Loop de descoberta inicial
    while (serverIP.empty()) {
        serverIP = client.discoverServer(discovery_port, request_port);
        if (serverIP.empty()) {
            cout << "Não foi possível encontrar um servidor. Tentando novamente em 5 segundos..." << endl;
            sleep(5);
        }
    }

    cout << "Conectado ao líder " << serverIP << ". Digite os números (Ctrl+D para encerrar):" << endl;
    
    uint32_t num;
    while (std::cin >> num) {
        // Tenta enviar o número. Se falhar, tenta redescobrir o servidor e reenviar.
        if (!client.sendOneRequest(num, serverIP, request_port)) {
            cout << "Falha na comunicação com o servidor " << serverIP << "." << endl;
            
            // Entra em modo de recuperação
            serverIP = "";
            while (serverIP.empty()) {
                cout << "Tentando encontrar um novo servidor..." << endl;
                serverIP = client.discoverServer(discovery_port, request_port);
                if (serverIP.empty()) sleep(5);
            }
            cout << "Reconectado ao líder " << serverIP << ". Reenviando número..." << endl;
            
            // Tenta reenviar o mesmo número para o novo líder
            if (!client.sendOneRequest(num, serverIP, request_port)) {
                 cout << "Falha ao reenviar o número. Desistindo deste número." << endl;
                 // Neste caso, optamos por não incrementar o 'current_seq' e pular o número.
                 // Uma lógica mais complexa poderia usar uma fila de não-confirmados.
            }
        }
    }

    cout << "Finalizando cliente." << endl;
    return 0;
}
