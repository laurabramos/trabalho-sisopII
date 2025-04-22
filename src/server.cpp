#include "server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <chrono>

// Mutex para sincronização da lista de participantes e soma total
std::mutex participantsMutex;
std::mutex sumMutex;

// Construtor do servidor
Server::Server() {
    // Criação do socket UDP
    serverSocket = createSocket(DISCOVERY_PORT);
    setSocketBroadcastOptions(serverSocket);
    setSocketTimeout(serverSocket, 3);
}

// Destrutor do servidor, fecha o socket ao encerrar
Server::~Server() {
    close(serverSocket);
}

void Server::printInicio() {
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[45];  // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

    strcat( buffer, " num_reqs 0 total_sum 0\n");

    std::cout << buffer;
}

// Inicia a escuta de mensagens de descoberta e números
void Server::startListening() {
    Message message;
    socklen_t clientLen = sizeof(clientAddr);

    // Inicia uma thread para receber números
    std::thread numberThread(&Server::receiveNumbers, this);
    numberThread.detach(); 

    std::cout << "Servidor esperando mensagens de descoberta...\n";
    
    while (true) {
        // Aguarda recebimento de mensagens de descoberta
        int received = recvfrom(serverSocket, &message, sizeof(Message), 0, 
                               (struct sockaddr*)&clientAddr, &clientLen);
        
        if (received > 0) {
            handleDiscovery(message, clientAddr);
        }
    }
}

// Processa mensagens de descoberta
void Server::handleDiscovery(Message& message, struct sockaddr_in &clientAddr) {
    if (message.type == Type::DESC) {  // Comparação com ENUM
        std::string serverIP = getIP();
        
        // Criar uma resposta como Message
        Message response;
        response.type = Type::DESC_ACK;

        // Envia a resposta ao cliente
        sendto(serverSocket, &response, sizeof(Message), 0, 
               (struct sockaddr*)&clientAddr, sizeof(clientAddr));

        std::string clientIP = inet_ntoa(clientAddr.sin_addr);// Obtém o IP do cliente

        bool test = checkList(clientIP); // Verifica se já está na lista

        if(test == false){
            participants.push_back({clientIP, 0, 0}); // Adiciona o cliente à lista de participantes
        }
        
        std::cout << "Novo cliente registrado: " << clientIP << std::endl;

    }
}

// Método para receber números enviados pelos clientes
void Server::receiveNumbers() {
    // Criação de um novo socket para receber números
    int numSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (numSocket == -1) {
        perror("Erro ao criar socket para números");
        return;
    }

    // Configuração do socket para recebimento de números
    struct sockaddr_in numAddr;
    memset(&numAddr, 0, sizeof(numAddr));
    numAddr.sin_family = AF_INET;
    numAddr.sin_addr.s_addr = INADDR_ANY;
    numAddr.sin_port = htons(RESQUEST_PORT);

    if (bind(numSocket, (struct sockaddr*)&numAddr, sizeof(numAddr)) < 0) {
        perror("Erro ao bindar socket de números");
        close(numSocket);
        return;
    }

    // Criar múltiplas threads para processar os números
    const int NUM_THREADS = 3;
    std::vector<std::thread> workers;

    for (int i = 0; i < NUM_THREADS; ++i) {
        workers.emplace_back([this, numSocket]() {
            while (true) {
                //std::cout << "Entrando na thread" << std::endl;
                Message number;//estrutura para troca de pacotes
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);

                // Aguarda recebimento de número
                int received = recvfrom(numSocket, &number, sizeof(Message), 0, 
                                       (struct sockaddr*)&clientAddr, &clientLen);
                if (received > 0) {
                    std::string clientIP = inet_ntoa(clientAddr.sin_addr);

                //std::cout << "Entrando no IF" << std::endl;
                    
                    {
                        
                        // Atualiza informações do participante
                        std::lock_guard<std::mutex> lock(participantsMutex);
                        updateParticipant(clientIP, number.num);
                    
                    }

                    {
                        // Atualiza a tabela de soma total
                        std::lock_guard<std::mutex> lock(sumMutex);
                        updateSumTable(number.seq, number.num);
                    }

                    printParticipants(); // Imprime a lista de participantes

                    // Envia confirmação para o cliente
                    Message confirmation = {Type::REQ_ACK, 0, number.seq};

                    sendto(numSocket, &confirmation, sizeof(Message), 0, 
                           (struct sockaddr*)&clientAddr, clientLen);

                           //std::cout << "Sera que to mandando algo?" << std::endl;
                }

               // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    close(numSocket);
}

// Imprime a lista de participantes e seus últimos valores recebidos
void Server::printParticipants() {
    std::cout << "Lista de participantes:\n";
    for (const auto& p : participants) {
        std::cout << "IP: " << p.address 
                  << ", Seq: " << p.last_req
                  << ", Num: " << p.last_sum << std::endl;
    }
}

// Verifica se um IP já está na lista de participantes
bool Server::checkList(const std::string& ip) {
    for (const auto& participant : participants) {
        if (participant.address == ip) {
            //std::cout << "Esta na list \n";
            return true; // IP encontrado na lista
        }
    }
    return false; // IP não está na lista
}

// Atualiza os dados de um participante
void Server::updateParticipant(const std::string& clientIP, uint32_t num) {

    for (auto& p : participants) {
        if (p.address == clientIP) {
            p.last_sum += num;
            p.last_req++;
            return;
        }
    }
    // Se não encontrou o IP, adiciona um novo participante
    participants.push_back({clientIP, num, 0});
}

// Atualiza a soma total das requisições

void Server::updateSumTable(uint32_t seq, uint64_t num) {

    sumTotal.num_reqs++;
    sumTotal.sum += num;
    if(sumTotal.num_reqs % 100000 == 0) {
        std::cout << "Total de requisições: " << sumTotal.num_reqs << std::endl;
        std::cout << "Soma total: " << sumTotal.sum << std::endl;}
}
