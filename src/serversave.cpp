/**
compilado de funções usadas para ativação e funcionamento do servidor, incluindo a criação de sockets, 
envio e recebimento de mensagens, e manipulação de participantes.
**/

#include "libs/serversave.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <chrono>
#include <bits/stdc++.h>
using namespace std;
// Mutex para sincronização da lista de participantes e soma total
mutex participantsMutex;
mutex sumMutex;
long long int numreqs = 0;
/*Funções utilizadas */

// Construtor do servidor
Server::Server(int Discovery_Port)
{
    // Criação do socket UDP
    serverSocket = createSocket(Discovery_Port);
    setSocketBroadcastOptions(serverSocket);
    setSocketTimeout(serverSocket, 3);
}

// Destrutor do servidor, fecha o socket ao encerrar
Server::~Server()
{
    close(serverSocket);
}

void Server::printInicio()
{
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[45]; // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

    strcat(buffer, " num_reqs 0 total_sum 0\n");

    cout << buffer;
}

// Inicia a escuta de mensagens de descoberta e números
void Server::startListening(int Request_Port)
{
    Message message;
    socklen_t clientLen = sizeof(clientAddr);

    // Inicia uma thread para receber números
    thread numberThread(&Server::receiveNumbers, this, Request_Port);
    numberThread.detach();


    while (true)
    {
        // Aguarda recebimento de mensagens de descoberta
        int received = recvfrom(serverSocket, &message, sizeof(Message), 0,
                                (struct sockaddr *)&clientAddr, &clientLen);

        if (received > 0)
        {
            handleDiscovery(message, clientAddr);
        }
    }
}

// Processa mensagens de descoberta
void Server::handleDiscovery(Message &message, struct sockaddr_in &clientAddr)
{
    if (message.type == Type::DESC)
    { // Comparação com ENUM
        string serverIP = getIP();

        // Criar uma resposta como Message
        Message response;
        response.type = Type::DESC_ACK;

        // Envia a resposta ao cliente
        sendto(serverSocket, &response, sizeof(Message), 0,
               (struct sockaddr *)&clientAddr, sizeof(clientAddr));

        string clientIP = inet_ntoa(clientAddr.sin_addr); // Obtém o IP do cliente

        bool test = checkList(clientIP); // Verifica se já está na lista

        if (test == false)
        {
            participants.push_back({clientIP, 0, 0}); // Adiciona o cliente à lista de participantes
            cout << "Novo cliente registrado: " << clientIP << endl;
        }
    }
}

void Server::receiveNumbers(int Request_Port)
{
    // Criação de um novo socket para receber números
    int numSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (numSocket == -1)
    {
        perror("Erro ao criar socket para números");
        return;
    }

    // Configuração do socket para recebimento de números
    struct sockaddr_in numAddr;
    memset(&numAddr, 0, sizeof(numAddr));
    numAddr.sin_family = AF_INET;
    numAddr.sin_addr.s_addr = INADDR_ANY;
    numAddr.sin_port = htons(Request_Port);

    if (bind(numSocket, (struct sockaddr *)&numAddr, sizeof(numAddr)) < 0)
    {
        perror("Erro ao bindar socket de números");
        close(numSocket);
        return;
    }

    // Criar múltiplas threads para processar os números
    const int NUM_THREADS = 3;
    vector<thread> workers;

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        workers.emplace_back([this, numSocket]()
                             {
            while (true) {
                Message number;//estrutura para troca de pacotes
                struct sockaddr_in clientAddr;
                socklen_t clientLen = sizeof(clientAddr);

                // Aguarda recebimento de número
                int received = recvfrom(numSocket, &number, sizeof(Message), 0, 
                                       (struct sockaddr*)&clientAddr, &clientLen);

                if (received > 0) {
                    string clientIP = inet_ntoa(clientAddr.sin_addr);
                    bool isDuplicate = false;
                                 
                    {
                        lock_guard<mutex> lock(participantsMutex);
                        isDuplicate = isDuplicateRequest(clientIP, number.seq);
                    }
                           
                    if (isDuplicate){
                        printRepet(clientIP, number);
                    }
                    else if (!isDuplicate) {
                        {
                            lock_guard<mutex> lock(participantsMutex);
                            updateParticipant(clientIP, number.seq, number.num);
                        }
                                
                        {
                        lock_guard<mutex> lock(sumMutex); // A função utiliza mutexes para garantir a consistência dos dados compartilhados entre threads.

                        updateSumTable(number.seq, number.num);
                        }
                        printParticipants(clientIP, number);
                    }
                                    
                    // Sempre envia o ACK, mesmo que seja duplicado
                    Message confirmation = {Type::REQ_ACK, 0, number.seq};
                    sendto(numSocket, &confirmation, sizeof(Message), 0,
                    (struct sockaddr *)&clientAddr, clientLen);
                                
                }
                                
            } });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    close(numSocket);
}

// Imprime a lista de participantes e seus últimos valores recebidos
void Server::printParticipants(const std::string &clientIP, const Message &number)
{
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[21]; // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

    lock_guard<mutex> lock(participantsMutex); // Protege acesso à lista

    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
                cout << buffer
                     << " client " << p.address
                     << " id_req " << p.last_req
                     << " value " << p.last_value
                     << " num_reqs " << sumTotal.num_reqs
                     << " total_sum " << sumTotal.sum << std::endl;
            
            break; // já encontrou o cliente, não precisa continuar
        }
    }
}

void Server::printRepet(const std::string &clientIP, const Message &number)
{
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[21]; // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);

    lock_guard<mutex> lock(participantsMutex); // Protege acesso à lista

    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
                cout << buffer
                     << " client " << p.address
                     << " DUP !! id_req " << p.last_req
                     << " value " << p.last_value
                     << " num_reqs " << sumTotal.num_reqs
                     << " total_sum " << sumTotal.sum << std::endl;
            
            break; // já encontrou o cliente, não precisa continuar
        }
    }
}


bool Server::isDuplicateRequest(const string &clientIP, uint32_t seq)
{
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[21]; // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ltm);
    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            return (p.last_req == seq); // true se já foi processado
        }
    }
    return false;
}

// Verifica se um IP já está na lista de participantes
bool Server::checkList(const std::string &ip)
{
    for (const auto &participant : participants)
    {
        if (participant.address == ip)
        {
            return true; // IP encontrado na lista
        }
    }
    return false; // IP não está na lista
}



void Server::updateParticipant(const string &clientIP, uint32_t seq, uint32_t num)
{
    for (auto &p : participants)
    {
        if (p.address == clientIP)
        {
            p.last_sum += num;
            p.last_req = seq;
            p.last_value = num;
            return;
        }
    }
    // Se for um novo IP, adiciona
    participants.push_back({clientIP, num, seq});
}

// Atualiza a soma total das requisições

void Server::updateSumTable(uint32_t seq, uint64_t num)
{
   
    sumTotal.num_reqs++;
    sumTotal.sum += num;

}


