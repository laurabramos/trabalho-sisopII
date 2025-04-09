#include "client.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>    // Para fcntl() e O_NONBLOCK
#include <sys/select.h>  // Para select()
#include <arpa/inet.h> // Para manipulação de endereços IP
#include <cstdint>
#include <fstream>

#define MAX_ATTEMPTS 5
#define TIMEOUT 2

// Construtor da classe Client
Client::Client() {
    // Criação do socket UDP para broadcast
    clientSocketBroad = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocketBroad == -1) {
        perror("Erro ao criar socket UDP");
        exit(1);
    }

    // Habilita a opção de broadcast no socket
    int broadcastEnable = 1;
    setsockopt(clientSocketBroad, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

     // Configuração do endereço de broadcast para envio de mensagens de descoberta (IPV4, UDP, Porta)
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;
    broadcastAddr.sin_port = htons(DISCOVERY_PORT);

    // Tornar o socket não bloqueante
    int flags = fcntl(clientSocketBroad, F_GETFL, 0);
    if (flags == -1) {
        perror("Erro ao obter flags do socket");
        close(clientSocketBroad);
        exit(1);
    }
    fcntl(clientSocketBroad, F_SETFL, flags | O_NONBLOCK);  // Configura para não bloqueante
}

// Destrutor da classe Client - Fecha o socket UDP quando o objeto é destruído
Client::~Client() {
    close(clientSocketBroad);
}

//descobrir servidor por broadcast
void Client::discoverServer() {
    Message message = {Type :: DESC}; //estrutura da troca de mensagens, tipo DESC
    int attempts = 0;
    
    while (attempts < MAX_ATTEMPTS) {
        // Envia mensagem de descoberta
        sendto(clientSocketBroad, &message, sizeof(Message), 0, 
            (struct sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        
        std::cout << "Mandando mensagem de descoberta...\n";

        // char buffer[BUFFER_SIZE];
        Message recMessage;; //armazena a resposta do servidor
        socklen_t serverLen = sizeof(broadcastAddr);
        int received = -1;
        
        // Usar select para verificar se há dados para ler no socket
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = TIMEOUT;    // Tempo máximo de espera
        timeout.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(clientSocketBroad, &read_fds);

        int selectResult = select(clientSocketBroad + 1, &read_fds, NULL, NULL, &timeout);
        
        if (selectResult > 0 && FD_ISSET(clientSocketBroad, &read_fds)) {
            // Se há dados disponíveis, tenta ler
            received = recvfrom(clientSocketBroad, &recMessage, sizeof(Message), 0, 
                               (struct sockaddr*)&broadcastAddr, &serverLen);
        }

        if (received > 0) {
            if (recMessage.type == Type::DESC_ACK) {// verifica se recebeu a resposta e se é do tipo correto DESC_ACK
                std::string serverIP = inet_ntoa(broadcastAddr.sin_addr); //retorna o ip do servidor
                std::cout << "Servidor encontrado! IP da resposta: " << serverIP << std::endl;
                sendNum(serverIP); // entra na funcao de envio dos numeros
                break;
            }
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

void Client::sendNum (const std::string& serverIP){
    //ESSE TRECHO DE CODIGO APENAS CRIA O SOCKET
    int clientSocketUni = socket(AF_INET, SOCK_DGRAM, 0);

    if (clientSocketUni == -1) {
        perror("Erro ao criar socket unicast");
        return;
    }

    //ESSE TRECHO DE CODIGO DEFINE PARA QUEM EU VOU MANDA O NUMERO, SE EU NAO SEI PARA QUEM EU VOU MANDAR O NUMERO E NAO CONSIGO ENVIAR

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(RESQUEST_PORT);

    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        perror("Erro ao converter endereço IP");
        close(clientSocketUni);
        return;
    }

    /*std::ifstream file(filename);
    if (!file) {
        std::cerr << "Erro ao abrir o arquivo: " << filename << std::endl;
        close(clientSocketUni);
        return;
    }*/

    uint32_t num;
    uint32_t seq = 1; //inicializa as variaveis

    while (std::cin >> num) {  // Lê números da entrada padrão (teclado)
        bool confirmed = false;

        //std::cout << "Primeiro while" << std::endl;


        while (!confirmed) {
            Message message = {Type::REQ, num, seq};//cria a mensagem do tipo Requisição, que envia os números

            if (sendto(clientSocketUni, &message, sizeof(Message), 0,
                       (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
                perror("Erro ao enviar número");
                close(clientSocketUni);
                return;
            }

            Message response; // estrutura para a mensagem de resposta do servidor
            socklen_t serverLen = sizeof(serverAddr);

            int received = recvfrom(clientSocketUni, &response, sizeof(Message), 0,
                                    (struct sockaddr*)&serverAddr, &serverLen);

            if (received > 0 && response.seq == seq) {// se for recebido uma mensagem o numero de sequencia da mensagem recebi for igual a da envia 
                //std::cout << "Confirmação recebida do servidor para requisição " << seq << std::endl;
                seq++;  // Incrementa a sequência para a próxima requisição
                confirmed = true;
            } else {
                std::cout << "Erro na confirmação do servidor. Reenviando requisição " << seq << "...\n";
            }
        }
    }

    close(clientSocketUni);
}


