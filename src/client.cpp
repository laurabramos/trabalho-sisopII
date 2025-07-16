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
#include <thread>
#include <mutex>

#define TIMEOUT_SEC 2
#define MAX_SEND_ATTEMPTS 3

using namespace std;

Client::Client(int discovery_port, int server_comm_port) {
    this->discovery_port = discovery_port;
    this->server_comm_port = server_comm_port;
}

Client::~Client() {
    stopListener();
}

bool Client::isRunning() const {
    return this->running;
}

void Client::startListener() {
    this->running = true;
    this->coordinator_listener_thread = std::thread(&Client::listenForCoordinator, this);
    cout << "[INFO] Listener de Coordenador iniciado." << endl;
}

void Client::stopListener() {
    this->running = false;
    // Pequena artimanha para desbloquear o recvfrom e permitir que a thread termine
    int sock = createSocket(0);
    if(sock != -1) {
        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(this->server_comm_port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        Message dummy_msg = {Type::DESC, 0, 0}; // Qualquer mensagem serve
        sendto(sock, &dummy_msg, sizeof(dummy_msg), 0, (struct sockaddr*)&addr, sizeof(addr));
        close(sock);
    }
    
    if (this->coordinator_listener_thread.joinable()) {
        this->coordinator_listener_thread.join();
    }
}

void Client::listenForCoordinator() {
    int listenerSocket = createSocket(this->server_comm_port);
    if (listenerSocket == -1) {
        cerr << "[ERRO] Não foi possível criar o socket do listener. A thread será encerrada." << endl;
        return;
    }
    setSocketTimeout(listenerSocket, 0); // Bloqueia indefinidamente até receber algo

    cout << "[LISTENER] Aguardando mensagens de broadcast do Coordenador na porta " << this->server_comm_port << endl;

    while (this->running) {
        Message msg;
        struct sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);

        recvfrom(listenerSocket, &msg, sizeof(Message), 0, (struct sockaddr*)&fromAddr, &fromLen);

        if (!this->running) break;

        if (msg.type == Type::COORDINATOR) {
            string new_leader_ip = inet_ntoa(fromAddr.sin_addr);
            
            lock_guard<mutex> lock(ip_mutex);
            if (this->serverIP != new_leader_ip) {
                time_t now = time(0);
                char buffer[100];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
                cout << endl << buffer << " [SISTEMA] Novo líder detectado em " << new_leader_ip << ". Atualizando endereço." << endl;
                this->serverIP = new_leader_ip;
            }
        }
    }
    close(listenerSocket);
    cout << "[INFO] Listener de Coordenador encerrado." << endl;
}

void Client::discoverInitialServer() {
    int discoverySocket = createSocket(0);
    if (discoverySocket == -1) {
        perror("Erro fatal ao criar socket de descoberta");
        exit(1); // Não é possível se recuperar disso
    }
    setSocketBroadcastOptions(discoverySocket);
    setSocketTimeout(discoverySocket, TIMEOUT_SEC);
    
    Message message = {Type::DESC, 0, 0}; 
    
    struct sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(this->discovery_port);
    broadcastAddr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    
    cout << "Procurando servidor inicial na rede (tentativas contínuas)..." << endl;

    while (this->running) {
        sendto(discoverySocket, &message, sizeof(message), 0,
               (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        
        Message recMessage;
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);
        int received = recvfrom(discoverySocket, &recMessage, sizeof(Message), 0,
                                (struct sockaddr*)&fromAddr, &fromLen);

        if (received > 0 && recMessage.type == Type::DESC_ACK) {
            string foundIP = inet_ntoa(fromAddr.sin_addr);
            
            lock_guard<mutex> lock(ip_mutex);
            this->serverIP = foundIP;
            
            cout << "Servidor líder encontrado em: " << this->serverIP << endl;
            close(discoverySocket); 
            return; // Sai da função e do loop
        }
        
        cout << "Nenhum líder encontrado. Tentando novamente em " << TIMEOUT_SEC << " segundos..." << endl;
        // O timeout no recvfrom já cria a pausa necessária.
    }
    close(discoverySocket);
}

bool Client::sendNum(int request_port) {
    string current_server_ip;
    {
        lock_guard<mutex> lock(ip_mutex);
        if (this->serverIP.empty()) {
            cerr << "[ERRO] IP do servidor desconhecido. Não é possível enviar." << endl;
            return false;
        }
        current_server_ip = this->serverIP;
    }

    int clientSocketUni = createSocket(0);
    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return false;
    }
    setSocketTimeout(clientSocketUni, TIMEOUT_SEC);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(request_port);
    inet_pton(AF_INET, current_server_ip.c_str(), &serverAddr.sin_addr);

    // Reenvia números não confirmados
    while(!this->unacked_nums.empty()) {
        uint32_t num_to_resend = this->unacked_nums.front();
        cout << "[SISTEMA] Reenviando número pendente (" << num_to_resend << ") para o servidor em " << current_server_ip << endl;
        bool confirmed = false;
        for (int attempts = 0; attempts < MAX_SEND_ATTEMPTS && !confirmed; ++attempts) {
            Message message = {Type::REQ, num_to_resend, this->current_seq, 0, 0, 0};
            sendto(clientSocketUni, &message, sizeof(Message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            Message response;
            if (recvfrom(clientSocketUni, &response, sizeof(Message), 0, NULL, NULL) > 0 &&
                response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                
                cout << "[SISTEMA] Reenvio confirmado. " << "id_req: " << response.seq << ", total_sum: " << response.total_sum << endl;
                this->current_seq++;
                confirmed = true;
                this->unacked_nums.pop();
            } else {
                 cout << "[AVISO] Tentativa " << attempts + 1 << " de reenvio falhou." << endl;
            }
        }
        if (!confirmed) {
            cout << "[ERRO] Falha ao reenviar número pendente para " << current_server_ip << ". A comunicação pode estar instável." << endl;
            close(clientSocketUni);
            return false;
        }
    }

    // Loop para ler novos números
    uint32_t num;
    cout << "Digite um número (ou Ctrl+D para sair): ";
    while (cin >> num) {
        bool confirmed = false;
        for (int send_attempts = 0; send_attempts < MAX_SEND_ATTEMPTS && !confirmed; ++send_attempts) {
            Message message = {Type::REQ, num, this->current_seq, 0, 0, 0};
            sendto(clientSocketUni, &message, sizeof(Message), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
            
            Message response;
            
            string response_ip;
            {
                 lock_guard<mutex> lock(ip_mutex);
                 response_ip = this->serverIP;
            }
             if (current_server_ip != response_ip) {
                 cout << "[SISTEMA] O líder mudou durante o envio. Abortando tentativa atual para usar o novo líder." << endl;
                 this->unacked_nums.push(num);
                 close(clientSocketUni);
                 return false;
             }
            
            if (recvfrom(clientSocketUni, &response, sizeof(Message), 0, NULL, NULL) > 0 && response.type == Type::REQ_ACK && response.seq == this->current_seq) {
                time_t now = time(0);
                char buffer[100];
                strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
                cout << buffer << " [OK] server " << current_server_ip
                     << " id_req " << response.seq
                     << " value " << num
                     << " total_sum " << response.total_sum << endl;

                this->current_seq++;
                confirmed = true;
            } else {
                cout << "[AVISO] Tentativa " << send_attempts + 1 << " de envio falhou. Reenviando..." << endl;
            }
        }

        if (!confirmed) {
            cout << "[ERRO] O servidor " << current_server_ip << " não está respondendo." << endl;
            cout << "[SISTEMA] Guardando o número " << num << " para tentar com um novo líder." << endl;
            this->unacked_nums.push(num);
            close(clientSocketUni);
            return false;
        }
        cout << "Digite um número (ou Ctrl+D para sair): ";
    }

    close(clientSocketUni);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Uso: " << argv[0] << " <porta_descoberta_cliente>" << endl;
        return 1;
    }

    int discovery_port = atoi(argv[1]);
    int request_port = discovery_port + 1;
    int server_comm_port = discovery_port + 2;

    Client client(discovery_port, server_comm_port);

    // Esta função agora bloqueia até que um líder seja encontrado.
    client.discoverInitialServer();

    client.startListener();

    while (client.isRunning()) {
        if (!client.sendNum(request_port)) {
            // Se sendNum falhar, a comunicação caiu.
            // O listener em background irá (eventualmente) detectar o novo líder.
            // O loop simplesmente espera e tenta novamente.
            cout << "[SISTEMA] Comunicação com o líder perdida. Aguardando novo líder ser anunciado..." << endl;
            this_thread::sleep_for(chrono::seconds(3));
        } else {
            // sendNum só retorna true se o stream de entrada (cin) terminar (EOF).
            cout << "Entrada finalizada. Encerrando o cliente." << endl;
            break;
        }
    }
    
    client.stopListener(); // Garante que a thread do listener seja finalizada corretamente

    return 0;
}