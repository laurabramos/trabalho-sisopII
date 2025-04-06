#include "server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>


Server::Server() {
    serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1) {
        perror("Erro ao criar socket UDP");
        exit(1);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(DISCOVERY_PORT);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Erro ao bindar socket UDP");
        exit(1);
    }
}

Server::~Server() {
    close(serverSocket);
}

void Server::startListening() {
    Message message;
    socklen_t clientLen = sizeof(clientAddr);

    std::thread numberThread(&Server::receiveNumbers, this);
    numberThread.detach(); 

    std::cout << "Servidor esperando mensagens de descoberta...\n";
    
    while (true) {
        int received = recvfrom(serverSocket, &message, sizeof(Message), 0, 
                               (struct sockaddr*)&clientAddr, &clientLen);
        
        if (received > 0) {
            handleDiscovery(message, clientAddr);
        }
    }
}


void Server::handleDiscovery(Message& message, struct sockaddr_in &clientAddr) {
    if (message.type == Type::DESC) {  // Comparação com ENUM
        std::string serverIP = getIP();
        
        // Criar uma resposta como Message
        Message response;
        response.type = Type::DESC_ACK;

        
        sendto(serverSocket, &response, sizeof(Message), 0, 
               (struct sockaddr*)&clientAddr, sizeof(clientAddr));

        std::string clientIP = inet_ntoa(clientAddr.sin_addr);

        bool test = checkList(clientIP);

        if(test == false){
            participants.push_back({clientIP, 0, 0}); // Adiciona o cliente à lista de participantes
        }
        
        std::cout << "Novo cliente registrado: " << clientIP << std::endl;

    }
}

void Server::receiveNumbers() {
    int numSocket = socket(AF_INET, SOCK_DGRAM, 0);
    int total = 0;

    if (numSocket == -1) {
        perror("Erro ao criar socket para números");
        return;
    }

    struct sockaddr_in numAddr;
    memset(&numAddr, 0, sizeof(numAddr));
    numAddr.sin_family = AF_INET;
    numAddr.sin_addr.s_addr = INADDR_ANY;
    numAddr.sin_port = htons(RESQUEST_PORT); // Porta específica para números

    if (bind(numSocket, (struct sockaddr*)&numAddr, sizeof(numAddr)) < 0) {
        perror("Erro ao bindar socket de números");
        close(numSocket);
        return;
    }

    std::cout << "Servidor pronto para receber números na porta 5001...\n";

    while (true) {
        Message number;
        //ESSE TRECHO DE CODIGO INSTANCIA UMA STRUCT QUE VAI RECEBER O ENDEREÇO QUE EU QUERO RECEBER O NUMERO
        //ELA ESTA NOMEADA COMO CLIENTEADDR PORQUE IREI RECEBER O ENDERECO DO CLIENTE
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        //O RECVFROM RETORNA A QUANTIDADE DE BYTES QUE EU RECEBI
        int received = recvfrom(numSocket, &number, sizeof(Message), 0, 
                               (struct sockaddr*)&clientAddr, &clientLen);
        if (received > 0) {
            std::string clientIP = inet_ntoa(clientAddr.sin_addr);

            updateParticipant(clientIP, number.num);
            updateSumTable(number.seq, number.num);
            printParticipants();
            total = number.num + total;
          
            Message confirmation = {Type::REQ_ACK, 0, number.seq};
           
            // Enviar resposta ao cliente
            sendto(numSocket, &confirmation, sizeof(Message), 0, 
                   (struct sockaddr*)&clientAddr, clientLen);


        }
    }

    close(numSocket);
}

void Server::printParticipants() {
    std::cout << "Lista de participantes:\n";
    for (const auto& p : participants) {
        std::cout << "IP: " << p.address 
                  << ", Seq: " << p.last_req
                  << ", Num: " << p.last_sum << std::endl;
    }
}

bool Server::checkList(const std::string& ip) {
    for (const auto& participant : participants) {
        if (participant.address == ip) {
            std::cout << "Esta na list \n";
            return true; // IP encontrado na lista
        }
    }
    return false; // IP não está na lista
}

void Server::updateParticipant(const std::string& clientIP, uint32_t num){

    for (auto& p : participants) {
        if (p.address == clientIP) {  // Verifica se o IP já está na lista
            p.last_sum += num;        // Atualiza o valor somando ao existente
            p.last_req++;            // Atualiza a sequência
            return;              // Sai da função, pois já atualizou
        }
    }



    // Se não encontrou o IP, adiciona um novo participante
    participants.push_back({clientIP, num, 0});
}

void Server::updateSumTable(uint32_t seq, uint64_t num){

    sumTotal.num_reqs ++;
    sumTotal.sum += num;
    std::cout << "Total de requisições: " << sumTotal.num_reqs << std::endl; 
    std::cout << "Soma total: " << sumTotal.sum << std::endl;
}