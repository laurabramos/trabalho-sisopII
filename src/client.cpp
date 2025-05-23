/**
 * Descobre o servidor na rede local enviando mensagens de broadcast e aguardando uma resposta.
 * 
 * Este método realiza a descoberta de um servidor na rede local utilizando mensagens de broadcast.
 * Ele envia uma mensagem de descoberta e aguarda uma resposta do tipo DESC_ACK. Caso receba a resposta,
 * obtém o endereço IP do servidor e chama a função `sendNum` para enviar informações adicionais.
 * 
 * @param Discovery_Port Porta utilizada para enviar e receber mensagens de descoberta.
 * @param Request_Port Porta utilizada para enviar informações adicionais ao servidor após a descoberta.
 * 
 * O método realiza até um número máximo de tentativas (definido por MAX_ATTEMPTS) para encontrar o servidor.
 * Caso não receba uma resposta dentro do tempo limite (definido por TIMEOUT), ele tenta novamente até atingir
 * o limite de tentativas.
 * 
 * Mensagens de erro são exibidas no console caso ocorram falhas no envio ou recebimento de mensagens.
 * 
 * Exemplo de saída no console:
 * - "Mandando mensagem de descoberta..."
 * - "Nenhuma resposta do servidor. Tentando novamente..."
 * - "YYYY-MM-DD HH:MM:SS <IP do servidor>"
 * - "Limite de tentativas atingido. Não foi possível encontrar o servidor."
 */

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
#define MAX_ATTEMPTS 5
#define TIMEOUT 2

using namespace std;

// Construtor da classe Client
Client::Client(int Discovery_Port) {
    // Criação do socket UDP para broadcast
    clientSocketBroad = createSocket(Discovery_Port);
    setSocketBroadcastOptions(clientSocketBroad);
}

// Destrutor
Client::~Client() {
    close(clientSocketBroad);
}

void printBytes(const void* data, size_t len) {
    //const uint8_t* bytes = static_cast<const uint8_t*>(data);
    //for (size_t i = 0; i < len; ++i) {
    //    printf("%02X ", bytes[i]);
    //}
    //printf("\n");
}

// Print inicial assim: Ao iniciar, o cliente deverá OBRIGATORIAMENTE exibir na tela uma mensagem informando a data e hora atual e o
// endereço IP do servidor (após a descoberta ser finalizada):
// 2024-10-01 18:37:00 server_addr 1.1.1.20

void Client::discoverServer(int Discovery_Port, int Request_Port) {
    Message message = {Type::DESC, 42, 15};
    printBytes(&message, sizeof(message));  // Deve imprimir exatamente 9 bytes se tudo estiver certo
    //cout << "sizeof(Message): " << sizeof(Message) << std::endl;  // Deve imprimir 9
    int attempts = 0;   

    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(Discovery_Port);
    broadcastAddr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    bzero(&(broadcastAddr.sin_zero), 8);
    
    //cout << "Mandando mensagem de descoberta...\n";
    while (attempts < MAX_ATTEMPTS) {
        // Envia broadcast
        ssize_t sent = sendto(clientSocketBroad, &message, sizeof(message), 0,
                              (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        if (sent == -1) {
            perror("Erro no sendto (broadcast)");
        }


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

            char buffer[21];  // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);

            cout << buffer << serverIP << endl;

            sendNum(serverIP,Request_Port);
            break;
        } else if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            cout << "Nenhuma resposta do servidor. Tentando novamente...\n";
            attempts++;
        }
    }


    if (attempts >= MAX_ATTEMPTS) {
        cout << "Limite de tentativas atingido. Não foi possível encontrar o servidor.\n";
    }
}

// Envia números via unicast
void Client::sendNum(const char *serverIP, int Request_Port) {
    int clientSocketUni = createSocket(Request_Port);
    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return;
    }
    setSocketTimeout(clientSocketUni,3);

    sockaddr_in serverAddr{};
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Request_Port);
    if (inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0)
    {
        cerr << "ERROR invalid address/ Address not supported." << endl;
        close(clientSocketUni);
        return;
    }

    uint32_t num;
    uint32_t soma = 0;
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
                    
                    soma+=num;

                    time_t now = time(0);
                    struct tm *ltm = localtime(&now);

                    char buffer[21];  // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
                    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);

                    cout << buffer << "server " << serverIP 
                                   << " id_req " << response.seq 
                                   << " value " << num 
                                   << " num_reqs " << response.seq 
                                   << " total_sum " << soma << endl;
                    seq++;
                    confirmed = true;
                } else {
                    cout << "Erro na confirmação do servidor. Reenviando requisição " << seq << "...\n";
                }
            }
        }
    }

    close(clientSocketUni);
}
int main(int argc, char* argv[]){
    int Discovery_Port;
    cerr << argv[1] << endl;
    Discovery_Port = atoi(argv[1]);
    int Request_Port = Discovery_Port + 1;
    //cout << "Começando client\n";

    Client client(Discovery_Port);
    client.discoverServer(Discovery_Port, Request_Port);
    return 0;
}
