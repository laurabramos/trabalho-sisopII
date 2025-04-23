/**
 *Função responsável por receber números de clientes via socket UDP e processá-los.
 * 
 * Esta função cria um socket UDP para receber números enviados por clientes. 
 * Ela utiliza múltiplas threads para processar os números recebidos de forma concorrente.
 * Cada número recebido é utilizado para atualizar informações de participantes e uma tabela de soma total.
 * Além disso, uma confirmação é enviada de volta ao cliente após o processamento.
 * 
 * @param Request_Port Porta na qual o servidor irá escutar para receber os números.
 * 
 * Detalhes do funcionamento:
 * - Um socket é criado e configurado para escutar na porta especificada.
 * - São criadas múltiplas threads para processar os números recebidos.
 * - Cada thread aguarda a recepção de mensagens contendo números.
 * - Ao receber uma mensagem, a função:
 *   1. Atualiza as informações do participante com base no IP do cliente.
 *   2. Atualiza a tabela de soma total com o número recebido.
 *   3. Imprime a lista de participantes.
 *   4. Envia uma mensagem de confirmação de volta ao cliente.
 * - O socket é fechado ao final da execução.
 * 
 * Erros possíveis:
 * - Falha na criação do socket.
 * - Falha ao realizar o bind do socket à porta especificada.
 * 
 * Nota:
 * - A função utiliza mutexes para garantir a consistência dos dados compartilhados entre threads.
 */

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
#include <bits/stdc++.h>
using namespace std;
// Mutex para sincronização da lista de participantes e soma total
mutex participantsMutex;
mutex sumMutex;
/*Funções utilizadas */

// Construtor do servidor
Server::Server(int Discovery_Port) {
    // Criação do socket UDP
    serverSocket = createSocket(Discovery_Port);
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

    cout << buffer;
}

// Inicia a escuta de mensagens de descoberta e números
void Server::startListening(int Request_Port) {
    Message message;
    socklen_t clientLen = sizeof(clientAddr);

    // Inicia uma thread para receber números
    thread numberThread(&Server::receiveNumbers, this, Request_Port);
    numberThread.detach(); 

    cout << "Servidor esperando mensagens de descoberta...\n";
    
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
        string serverIP = getIP();
        
        // Criar uma resposta como Message
        Message response;
        response.type = Type::DESC_ACK;

        // Envia a resposta ao cliente
        sendto(serverSocket, &response, sizeof(Message), 0, 
               (struct sockaddr*)&clientAddr, sizeof(clientAddr));

        string clientIP = inet_ntoa(clientAddr.sin_addr);// Obtém o IP do cliente

        bool test = checkList(clientIP); // Verifica se já está na lista

        if(test == false){
            participants.push_back({clientIP, 0, 0}); // Adiciona o cliente à lista de participantes
        }
        
        cout << "Novo cliente registrado: " << clientIP << endl;

    }
}


void Server::receiveNumbers(int Request_Port) {
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
    numAddr.sin_port = htons(Request_Port);

    if (bind(numSocket, (struct sockaddr*)&numAddr, sizeof(numAddr)) < 0) {
        perror("Erro ao bindar socket de números");
        close(numSocket);
        return;
    }

    // Criar múltiplas threads para processar os números
    const int NUM_THREADS = 3;
    vector<thread> workers;

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
                    string clientIP = inet_ntoa(clientAddr.sin_addr);

                //std::cout << "Entrando no IF" << std::endl;
                    
                    {
                        
                        // Atualiza informações do participante
                        lock_guard<mutex> lock(participantsMutex);
                        updateParticipant(clientIP, number.num);
                    
                    }

                    {
                        // Atualiza a tabela de soma total
                        lock_guard<mutex> lock(sumMutex);
                        updateSumTable(number.seq, number.num);
                    }

                    printParticipants(number); // Imprime a lista de participantes

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
void Server::printParticipants(Message number) {
    time_t now = time(0);
    struct tm *ltm = localtime(&now);

    char buffer[21];  // espaço suficiente para "YYYY-MM-DD HH:MM:SS\0"
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S ", ltm);


    for (const auto& p : participants) {
        cout << buffer 
                  << "client " << p.address 
                  << " id_req " << p.last_req
                  << " value " << number.num
                  << " num_reqs " << sumTotal.num_reqs
                  << " total_sum " << p.last_sum << std::endl;
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
void Server::updateParticipant(const string& clientIP, uint32_t num) {

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
        cout << "Total de requisições: " << sumTotal.num_reqs << endl;
        cout << "Soma total: " << sumTotal.sum << endl;}
}

int main(int argc, char *argv[]) {
    int Discovery_Port; 
    cerr << argv[1] << endl;
    Discovery_Port = atoi(argv[1]);
    cout << "Começando server\n";
    int Request_Port = Discovery_Port + 1;

    Server server(Discovery_Port);
    server.printInicio();
    server.startListening(Request_Port);
}