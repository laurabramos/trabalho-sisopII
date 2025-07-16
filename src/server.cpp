#include "libs/server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <chrono>
#include <vector>
#include <thread>
#include <string>
#include <algorithm>

using namespace std;

// Mutexes para garantir acesso seguro a recursos compartilhados entre threads.
mutex participantsMutex; // Protege a lista de clientes (participantes).
mutex sumMutex; // Protege a tabela de agregação (soma total).
mutex serverListMutex; // Protege a lista de servidores conhecidos.

/**
 * @brief Adiciona um timestamp a uma mensagem de log e a imprime no console.
 * @param message A mensagem a ser registrada.
 */
void log_with_timestamp(const string &message)
{
     time_t now = time(0);
     char timestamp[100];
     strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
     cout << timestamp << " " << message << endl;
}

/**
 * @brief Função de utilidade para converter uma string de endereço IP para um inteiro de 32 bits.
 * @param ipStr A string do IP (ex: "192.168.1.1").
 * @return O IP em formato de inteiro.
 */
uint32_t ipToInt(const std::string &ipStr)
{
     struct in_addr ip_addr;
     inet_aton(ipStr.c_str(), &ip_addr);
     return ntohl(ip_addr.s_addr);
}

/**
 * @brief Construtor da classe Server. Inicializa as portas, o IP e o estado inicial.
 * @param client_port Porta para descoberta de clientes.
 * @param req_port Porta para requisições de clientes.
 * @param server_comm_port Porta para comunicação entre servidores.
 */
Server::Server(int client_port, int req_port, int server_comm_port)
{
     this->client_discovery_port = client_port;
     this->client_request_port = req_port;
     this->server_communication_port = server_comm_port;
     this->my_ip = getIP();
     this->server_socket = -1;
     this->client_socket = -1;
     this->role = ServerRole::NEEDS_ELECTION; // Inicia precisando de uma eleição.
}

/**
 * @brief Destrutor da classe Server. Fecha os sockets abertos.
 */
Server::~Server()
{
     if (server_socket != -1)
        close(server_socket);
     if (client_socket != -1)
        close(client_socket);
}

// --- ARQUITETURA PRINCIPAL ---

/**
 * @brief Inicia o servidor, disparando threads de comunicação e gerenciando o ciclo de vida (eleição, operação como líder/backup).
 */
void Server::start()
{
     // Threads para comunicação com clientes rodam em paralelo e continuamente.
    thread client_comm_thread(&Server::receiveNumbers, this);
     thread client_discovery_thread(&Server::listenForClientDiscovery, this);

     // Loop principal que gerencia o estado do servidor.
    while (true)
   
 {
         // Fase 1: Encontra outros servidores e realiza uma eleição.
        findAndElect();
         
        // Fase 2: Opera como líder ou backup até que uma reeleição seja necessária.
        while (role == ServerRole::LEADER || role == ServerRole::BACKUP)
       
 {
             if (role == ServerRole::LEADER)
           
 {
                 runAsLeader();
            
 }
             else            
 {
                 runAsBackup();
            
 }
        
 }
         
        // Se o loop de operação for quebrado, significa que uma reeleição é necessária.
        log_with_timestamp("[" + my_ip + "] Evento de re-eleição despoletado. Reiniciando processo...");
         if (server_socket != -1) // Fecha o socket antigo para reabrir na nova fase de eleição.
       
 {
             close(server_socket);
             server_socket = -1;
        
 }
         this_thread::sleep_for(chrono::seconds(2)); // Pausa antes de recomeçar.
    
 }
     
    // Aguarda o término das threads (embora o loop acima seja infinito).
    client_comm_thread.join();
     client_discovery_thread.join();
}

/**
 * @brief Implementa o processo de descoberta de outros servidores e eleição de um líder.
 * O critério de eleição é o maior endereço IP.
 */
void Server::findAndElect()
{
 log_with_timestamp("[" + my_ip + "] --- INICIANDO FASE DE DESCOBERTA E ELEIÇÃO ---");
 leader_ip = "";
 server_list.clear();

 // VARIÁVEL DE CONTROLE: indica se recebemos um estado proativamente.
 bool handover_received = false;

 // Limpa o estado local. Se recebermos um estado novo, ele será preenchido.
 // Se não recebermos nada e nos tornarmos líder, começaremos do zero, o que é o correto.
 participants.clear();
 sumTotal = {0, 0};

 server_socket = createSocket(server_communication_port);
 if (server_socket == -1)
 exit(1);
 setSocketBroadcastOptions(server_socket);

 log_with_timestamp("[" + my_ip + "] Anunciando e descobrindo outros servidores...");
 Message discovery_msg = {Type::SERVER_DISCOVERY};
 struct sockaddr_in broadcast_addr = {};
 broadcast_addr.sin_family = AF_INET;
 broadcast_addr.sin_port = htons(server_communication_port);
 inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);

 for (int i = 0; i < 2; ++i)
 {
 sendto(server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
 this_thread::sleep_for(chrono::milliseconds(250));
 }

 setSocketTimeout(server_socket, 3);
 auto listen_start_time = chrono::steady_clock::now();
 while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - listen_start_time).count() < 3)
 {
 Message response;
 struct sockaddr_in from_addr;
 socklen_t from_len = sizeof(from_addr);
 if (recvfrom(server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
 {
 string from_ip = inet_ntoa(from_addr.sin_addr);
 if (from_ip == my_ip)
 continue;

 // ================================================================== //
 // ====> AQUI ESTÁ A MUDANÇA <==== //
 // Em vez de só adicionar à lista, verificamos o tipo da mensagem. //
 // ================================================================== //
 switch (response.type)
 {
 case Type::SERVER_DISCOVERY:
 {
 // Comportamento original: outro servidor se anuncia.
 lock_guard<mutex> lock(serverListMutex);
 if (!checkList(from_ip))
 {
 server_list.push_back({from_ip});
 log_with_timestamp("[" + my_ip + "] Servidor " + from_ip + " detectado.");
 }
 break;
 }
 case Type::STATE_TRANSFER_PAYLOAD:
 {
 // Comportamento novo: recebendo estado de um líder antigo.
 handover_received = true; // Marca que recebemos estado.
 log_with_timestamp("[" + my_ip + "] Recebendo estado proativo de " + from_ip);
 if (response.ip_addr == 0) // Pacote de estado global
 {
 lock_guard<mutex> lock(sumMutex);
 sumTotal.sum = response.total_sum_server;
 sumTotal.num_reqs = response.total_reqs_server;
 }
 else // Pacote de estado de participante
 {
 struct in_addr client_addr = {.s_addr = response.ip_addr};
 setParticipantState(inet_ntoa(client_addr), response.seq, response.num, response.total_sum, response.seq);
 }
 break;
 }
 }
 }
 }

 // Adiciona a si mesmo na lista para participar da eleição.
 server_list.push_back({my_ip});
 string new_leader_ip = my_ip;

 // Algoritmo de eleição: o servidor com o maior IP vence.
 for (const auto &server : server_list)
 {
 if (ipToInt(server.ip_address) > ipToInt(new_leader_ip))
 {
 new_leader_ip = server.ip_address;
 }
 }
 this->leader_ip = new_leader_ip;

 // Se este nó NÃO foi eleito líder, ele deve pedir o estado ao líder eleito.
 // Esta lógica não é executada se o nó se elege líder, preservando o estado que
 // ele pode ter acabado de receber proativamente.
 if (this->leader_ip != this->my_ip)
 {
 log_with_timestamp("[" + my_ip + "] Nó não eleito. Solicitando estado do líder " + this->leader_ip + "...");

 // A limpeza de estado foi movida para o início da função.
 // O pedido de estado continua aqui, como fallback para o caso de um backup normal.
 Message request_msg = {Type::STATE_TRANSFER_REQUEST};
 struct sockaddr_in leader_addr = {};
 leader_addr.sin_family = AF_INET;
 leader_addr.sin_port = htons(server_communication_port);
 inet_pton(AF_INET, this->leader_ip.c_str(), &leader_addr.sin_addr);
 sendto(server_socket, &request_msg, sizeof(request_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));

 // ... O loop para receber o estado como backup continua aqui ...
 }
 else // Se ESTE nó foi eleito líder
 {
 if (handover_received)
 {
 log_with_timestamp("[" + my_ip + "] Eleito líder com estado herdado.");
 }
 else
 {
 log_with_timestamp("[" + my_ip + "] Eleito líder. Iniciando com estado novo.");
 }
 }

 // Define o papel (role) final com base no resultado da eleição.
 if (this->leader_ip == this->my_ip)
 {
 this->role = ServerRole::LEADER;
 }
 else
 {
 this->role = ServerRole::BACKUP;
 }
 log_with_timestamp("[" + my_ip + "] Eleição concluída. Função final: " + (this->role == ServerRole::LEADER ? "LÍDER" : "BACKUP") + ". Líder definido como: " + this->leader_ip);
}

// --- LÓGICA DE OPERAÇÃO ---

/**
 * @brief Define o comportamento do servidor quando ele é o líder.
 * Dispara threads para enviar heartbeats e ouvir mensagens de outros servidores.
 */
void Server::runAsLeader()
{
     log_with_timestamp("--- MODO LÍDER ATIVADO ---");
     this->leader_ip = my_ip;
     thread heartbeat_thread(&Server::sendHeartbeats, this); // Anuncia sua presença.
     thread server_listener_thread(&Server::listenForServerMessages, this); // Ouve outros servidores.

     // Mantém-se neste loop enquanto for o líder.
    while (role == ServerRole::LEADER)
   
 {
         this_thread::sleep_for(chrono::seconds(1));
    
 }
     
    log_with_timestamp("Deixando o papel de líder...");
     heartbeat_thread.join();
     server_listener_thread.join();
}

/**
 * @brief Define o comportamento do servidor quando ele é um backup.
 * Ouve mensagens do líder (heartbeats, atualizações) e monitora sua falha.
 */
void Server::runAsBackup()
{
     log_with_timestamp("--- MODO BACKUP ATIVADO. Líder: " + this->leader_ip + " ---");
     log_with_timestamp("[" + my_ip + "] [BACKUP] Solicitando estado completo do líder " + this->leader_ip);
     
    // Solicita o estado completo do líder para garantir consistência.
    Message request_msg = {Type::STATE_TRANSFER_REQUEST, 0, 0};
     struct sockaddr_in leader_addr = {};
     leader_addr.sin_family = AF_INET;
     leader_addr.sin_port = htons(server_communication_port);
     inet_pton(AF_INET, this->leader_ip.c_str(), &leader_addr.sin_addr);
     sendto(server_socket, &request_msg, sizeof(request_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));

     last_heartbeat_time = chrono::steady_clock::now();
     
    // Threads para detectar falha do líder e para ouvir mensagens do líder.
    thread failure_detection_thread(&Server::checkForLeaderFailure, this);
     thread backup_listener_thread(&Server::listenForBackupMessages, this);
     
    // Mantém-se neste loop enquanto for um backup.
    while (role == ServerRole::BACKUP)
   
 {
         this_thread::sleep_for(chrono::seconds(1));
    
 }
     
    failure_detection_thread.join();
     backup_listener_thread.join();
}

// --- THREADS E HANDLERS ---

/**
 * @brief (LÍDER) Trata um pedido de transferência de estado de um servidor backup.
 * Envia o estado global e o estado de cada participante individualmente.
 * @param fromAddr O endereço do servidor que solicitou o estado.
 */
void Server::handleStateTransferRequest(const struct sockaddr_in &fromAddr)
{
     string requester_ip = inet_ntoa(fromAddr.sin_addr);
     log_with_timestamp("[" + my_ip + "] [LEADER] Recebido pedido de transferência de estado de " + requester_ip);
     
    // Bloqueia os mutexes para ler o estado de forma segura.
    lock_guard<mutex> lock_p(participantsMutex);
     lock_guard<mutex> lock_s(sumMutex);
     
    // Envia o estado global (soma total e número de requisições).
    Message global_state_msg = {};
     global_state_msg.type = Type::STATE_TRANSFER_PAYLOAD;
     global_state_msg.total_sum_server = sumTotal.sum;
     global_state_msg.total_reqs_server = sumTotal.num_reqs;
     sendto(server_socket, &global_state_msg, sizeof(global_state_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

     // Envia o estado de cada cliente (participante) um por um.
    for (const auto &p : participants)
   
 {
         Message participant_msg = {};
         participant_msg.type = Type::STATE_TRANSFER_PAYLOAD;
         inet_pton(AF_INET, p.address.c_str(), &participant_msg.ip_addr);
         participant_msg.seq = p.last_req;
         participant_msg.num = p.last_value;
         participant_msg.total_sum = p.last_sum;
         sendto(server_socket, &participant_msg, sizeof(participant_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
         this_thread::sleep_for(chrono::milliseconds(10)); // Pequena pausa para evitar sobrecarregar o receptor.
    
 }
     log_with_timestamp("[" + my_ip + "] [LEADER] Transferência de estado para " + requester_ip + " concluída.");
}

/**
 * @brief (BACKUP) Verifica periodicamente se o líder ainda está ativo.
 * Se não receber um heartbeat por um tempo (HEARTBEAT_TIMEOUT_SEC), assume que o líder falhou e força uma nova eleição.
 */
void Server::checkForLeaderFailure()
{
     last_heartbeat_time = chrono::steady_clock::now();
     const int HEARTBEAT_TIMEOUT_SEC = 6;
     while (role == ServerRole::BACKUP)
   
 {
         this_thread::sleep_for(chrono::seconds(1));
         if (leader_ip.empty() || leader_ip == my_ip)
       
 {
             continue;
        
 }
         
        // Verifica o tempo desde o último heartbeat.
        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count() > HEARTBEAT_TIMEOUT_SEC)
       
 {
             log_with_timestamp("[" + my_ip + "] Líder (" + leader_ip + ") inativo. Reiniciando para forçar nova eleição.");
             role = ServerRole::NEEDS_ELECTION; // Muda o estado para acionar a reeleição no loop principal.
             return;
        
 }
    
 }
}

/**
 * @brief (LÍDER) Envia mensagens de heartbeat em broadcast periodicamente para informar que está ativo.
 */
void Server::sendHeartbeats()
{
     struct sockaddr_in broadcast_addr = {};
     broadcast_addr.sin_family = AF_INET;
     broadcast_addr.sin_port = htons(server_communication_port);
     inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
     Message hb_msg = {Type::HEARTBEAT};
     while (role == ServerRole::LEADER)
   
 {
         sendto(server_socket, &hb_msg, sizeof(hb_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
         this_thread::sleep_for(chrono::seconds(2));
    
 }
}

/**
 * @brief (LÍDER) Ouve por mensagens de outros servidores.
 * Pode ser um pedido de descoberta ou um pedido de transferência de estado.
 */
void Server::listenForServerMessages()
{
     while (role == ServerRole::LEADER)
   
 {
         setSocketTimeout(server_socket, 1); // Timeout curto para não bloquear o loop.
         Message msg;
         struct sockaddr_in from_addr;
         socklen_t from_len = sizeof(from_addr);
         if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
       
 {
             string from_ip = inet_ntoa(from_addr.sin_addr);
             if (from_ip == my_ip) continue; // Ignora as próprias mensagens.
             
            // Direciona a mensagem para o handler apropriado.
            if (msg.type == Type::SERVER_DISCOVERY)
           
 {
                 handleServerDiscovery(from_addr);
            
 }
             else if (msg.type == Type::STATE_TRANSFER_REQUEST)
           
 {
                 handleStateTransferRequest(from_addr);
            
 }
        
 }
    
 }
}

/**
 * @brief (LÍDER) Trata uma mensagem de descoberta de um novo servidor.
 * Adiciona o novo servidor à lista e verifica se precisa ceder a liderança (se o novo servidor tiver um IP maior).
 * @param fromAddr O endereço do novo servidor.
 */
void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr)
{
     string new_server_ip = inet_ntoa(fromAddr.sin_addr);
     lock_guard<mutex> lock(serverListMutex);
     
    // Adiciona o servidor à lista se ele for novo.
    if (!checkList(new_server_ip))
   
 {
         server_list.push_back({new_server_ip});
         log_with_timestamp("[" + my_ip + "] Novo servidor " + new_server_ip + " adicionado à lista.");
    
 }
     
    // Se o novo servidor tem um IP maior, o líder atual deve ceder e iniciar uma nova eleição.
    if (this->role == ServerRole::LEADER && ipToInt(new_server_ip) > ipToInt(my_ip))
   
 {
 log_with_timestamp("[" + my_ip + "] Detectado servidor com IP maior (" + new_server_ip + "). Iniciando transferência de estado proativa...");

 // 1. Chamar uma nova função para transferir o estado para o 'new_server_ip'.
 proactiveStateTransfer(new_server_ip);

 // 2. Apenas depois da transferência bem-sucedida, ceder a liderança.
 log_with_timestamp("[" + my_ip + "] Transferência concluída. Cedendo liderança.");
 this->role = ServerRole::NEEDS_ELECTION;
    
 }
}

void Server::proactiveStateTransfer(const std::string &successor_ip)
{
 // Bloqueia os recursos para garantir uma cópia consistente do estado.
 lock_guard<mutex> lock_p(participantsMutex);
 lock_guard<mutex> lock_s(sumMutex);

 // Configura o endereço do servidor sucessor.
 struct sockaddr_in successor_addr = {};
 successor_addr.sin_family = AF_INET;
 successor_addr.sin_port = htons(this->server_communication_port);
 inet_pton(AF_INET, successor_ip.c_str(), &successor_addr.sin_addr);

 // Envia o estado global (tabela de agregação).
 Message global_state_msg = {};
 global_state_msg.type = Type::STATE_TRANSFER_PAYLOAD;
 global_state_msg.total_sum_server = sumTotal.sum;
 global_state_msg.total_reqs_server = sumTotal.num_reqs;
 sendto(server_socket, &global_state_msg, sizeof(global_state_msg), 0, (struct sockaddr *)&successor_addr, sizeof(successor_addr));
 this_thread::sleep_for(chrono::milliseconds(10));

 // Envia o estado de cada participante (tabela de clientes).
 for (const auto &p : participants)
 {
 Message participant_msg = {};
 participant_msg.type = Type::STATE_TRANSFER_PAYLOAD;
 inet_pton(AF_INET, p.address.c_str(), &participant_msg.ip_addr);
 participant_msg.seq = p.last_req;
 participant_msg.num = p.last_value;
 participant_msg.total_sum = p.last_sum;
 sendto(server_socket, &participant_msg, sizeof(participant_msg), 0, (struct sockaddr *)&successor_addr, sizeof(successor_addr));
 this_thread::sleep_for(chrono::milliseconds(10));
 }
}

/**
 * @brief (BACKUP) Ouve por mensagens do líder.
 * Processa heartbeats, atualizações de estado (replicação) e transferências de estado completas.
 */
void Server::listenForBackupMessages()
{
     while (role == ServerRole::BACKUP)
   
 {
         setSocketTimeout(server_socket, 1);
         Message msg;
         struct sockaddr_in from_addr;
         socklen_t from_len = sizeof(from_addr);
         if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
       
 {
             string from_ip = inet_ntoa(from_addr.sin_addr);
             
            // Reseta o timer de falha ao receber um heartbeat do líder.
            if (msg.type == Type::HEARTBEAT && from_ip == this->leader_ip)
           
 {
                 last_heartbeat_time = chrono::steady_clock::now();
            
 }
             // Recebe uma atualização de estado (replicação) do líder.
            else if (msg.type == Type::REPLICATION_UPDATE && from_ip == this->leader_ip)
           
 {
                 log_with_timestamp("[" + my_ip + "] [BACKUP] Recebida atualização de estado do líder.");
                 applyReplicationState(msg); // Aplica a mudança localmente.
                 // Envia uma confirmação (ACK) para o líder.
                Message ack_msg = {Type::REPLICATION_ACK, msg.seq, 0};
                 sendto(server_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr *)&from_addr, from_len);
            
 }
             // Recebe pacotes de uma transferência de estado completa.
            else if (msg.type == Type::STATE_TRANSFER_PAYLOAD && from_ip == this->leader_ip)
           
 {
                 if (msg.ip_addr == 0) // Pacote com estado global.
               
 {
                     lock_guard<mutex> lock(sumMutex);
                     sumTotal.sum = msg.total_sum_server;
                     sumTotal.num_reqs = msg.total_reqs_server;
                     log_with_timestamp("[" + my_ip + "] [BACKUP] Estado global sincronizado: " + to_string(sumTotal.sum));
                
 }
                 else // Pacote com estado de um participante.
               
 {
                     struct in_addr client_addr = {.s_addr = msg.ip_addr};
                     setParticipantState(inet_ntoa(client_addr), msg.seq, msg.num, msg.total_sum, msg.seq);
                     log_with_timestamp("[" + my_ip + "] [BACKUP] Estado do participante " + string(inet_ntoa(client_addr)) + " sincronizado.");
                
 }
            
 }
        
 }
    
 }
}

/**
 * @brief Ouve continuamente por pedidos de descoberta de clientes.
 * Apenas o líder responde a esses pedidos.
 */
void Server::listenForClientDiscovery()
{
     int discovery_socket = createSocket(this->client_discovery_port);
     if (discovery_socket == -1)
   
 {
         log_with_timestamp("[" + my_ip + "] ERRO CRÍTICO: Falha ao criar socket de descoberta de cliente.");
         return;
    
 }
     while (true)
   
 {
         Message msg;
         struct sockaddr_in clientAddr;
         socklen_t clientLen = sizeof(clientAddr);
         int received = recvfrom(discovery_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&clientAddr, &clientLen);
         
        if (received > 0 && msg.type == Type::DESC)
       
 {
             // Apenas o líder deve responder.
            if (this->role == ServerRole::LEADER)
           
 {
                 log_with_timestamp("[" + my_ip + "] [LEADER] Recebido pedido de descoberta de " + string(inet_ntoa(clientAddr.sin_addr)));
                 handleClientDiscovery(discovery_socket, clientAddr);
            
 }
        
 }
    
 }
     close(discovery_socket);
}

/**
 * @brief Ouve continuamente por requisições (números) de clientes.
 * Apenas o líder processa as requisições. Backups redirecionam para o líder.
 */
void Server::receiveNumbers()
{
     int numSocket = createSocket(client_request_port);
     if (numSocket == -1)
   
 {
         return;
    
 }
     while (true)
   
 {
         Message number;
         struct sockaddr_in clientAddr;
         socklen_t clientLen = sizeof(clientAddr);
         int received = recvfrom(numSocket, &number, sizeof(Message), 0, (struct sockaddr *)&clientAddr, &clientLen);

         if (received > 0 && number.type == Type::REQ)
       
 {
             // Se este servidor for o líder, processa a requisição.
            if (role == ServerRole::LEADER)
           
 {
                 string clientIP = inet_ntoa(clientAddr.sin_addr);
                 uint32_t last_known_seq = 0;
                 
                // Verifica o último número de sequência conhecido para este cliente.
               
 {
                     lock_guard<mutex> lock(participantsMutex);
                     for (const auto &p : participants)
                   
 {
                         if (p.address == clientIP)
                       
 {
                             last_known_seq = p.last_req;
                             break;
                        
 }
                    
 }
                
 }

                 // Se a requisição for a próxima na sequência, processa-a.
                if (number.seq == last_known_seq + 1)
               
 {
                     tableClient clientState = updateParticipant(clientIP, number.seq, number.num);
                     updateSumTable(number.seq, number.num);
                     printParticipants(clientIP, "[LEADER] ");
                     
                    tableAgregation server_state_copy;
                    
 {
                         lock_guard<mutex> lock(sumMutex);
                         server_state_copy = this->sumTotal;
                    
 }
                     
                    // Replicação da atualização para os servidores backup.
                    replicateToBackups(number, clientAddr, clientState, server_state_copy);
                     
                    // Envia a confirmação para o cliente.
                    Message confirmation = {};
                     confirmation.type = Type::REQ_ACK;
                     confirmation.seq = number.seq;
                     confirmation.total_sum = clientState.last_sum;
                     confirmation.total_reqs = clientState.last_req;
                     sendto(numSocket, &confirmation, sizeof(confirmation), 0, (struct sockaddr *)&clientAddr, clientLen);
                
 }
                 // Se for uma requisição duplicada (já processada), reenvia a confirmação anterior.
                else if (number.seq <= last_known_seq)
               
 {
                     printRepet(clientIP, number.seq);
                     Message confirmation = {};
                     confirmation.type = Type::REQ_ACK;
                     confirmation.seq = number.seq;
                    
 {
                         lock_guard<mutex> lock(participantsMutex);
                         for (const auto &p : participants)
                       
 {
                             if (p.address == clientIP)
                           
 {
                                 confirmation.total_sum = p.last_sum;
                                 confirmation.total_reqs = p.last_req;
                                 break;
                            
 }
                        
 }
                    
 }
                     sendto(numSocket, &confirmation, sizeof(confirmation), 0, (struct sockaddr *)&clientAddr, clientLen);
                
 }
                 // Se for uma requisição futura, descarta-a.
                else                
 {
                     log_with_timestamp("[" + my_ip + "] Requisição futura de " + clientIP + " descartada (esperado: " + to_string(last_known_seq + 1) + ", recebido: " + to_string(number.seq) + ")");
                
 }
            
 }
             // Se não for o líder, informa ao cliente quem é o líder atual.
            else            
 {
                 Message redirect_msg = {Type::NOT_LEADER};
                 if (!leader_ip.empty())
               
 {
                     inet_pton(AF_INET, leader_ip.c_str(), &redirect_msg.ip_addr);
                
 }
                 sendto(numSocket, &redirect_msg, sizeof(redirect_msg), 0, (struct sockaddr *)&clientAddr, clientLen);
            
 }
        
 }
    
 }
     close(numSocket);
}

/**
 * @brief Imprime uma mensagem inicial de estado.
 */
void Server::printInicio() { log_with_timestamp("num_reqs 0 total_sum 0"); }

/**
 * @brief (LÍDER) Trata um pedido de descoberta de um cliente.
 * Responde ao cliente e o adiciona à lista de participantes.
 * @param discovery_socket O socket usado para a comunicação.
 * @param fromAddr O endereço do cliente.
 */
void Server::handleClientDiscovery(int discovery_socket, const struct sockaddr_in &fromAddr)
{
     string clientIP = inet_ntoa(fromAddr.sin_addr);
     // Responde ao cliente para confirmar que este é o líder.
    Message response = {Type::DESC_ACK, 0, 0};
     sendto(discovery_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
     
    // Adiciona o cliente à lista de participantes se for a primeira vez.
    lock_guard<mutex> lock(participantsMutex);
     bool found = false;
     for (const auto &p : participants)
 {
         if (p.address == clientIP)
 {
             found = true;
             break;
        
 }
    
 }
     if (!found)
   
 {
         participants.push_back({clientIP, 0, 0, 0});
    
 }
}

/**
 * @brief (LÍDER) Replica uma atualização de estado para todos os servidores backup.
 * Envia a atualização e espera uma confirmação (ACK) de cada um.
 * @param client_request A mensagem original da requisição do cliente.
 * @param client_addr O endereço do cliente.
 * @param client_state O estado atualizado do cliente específico.
 * @param server_state O estado global atualizado do servidor.
 * @return true se a replicação foi bem-sucedida para todos os backups.
 */
bool Server::replicateToBackups(const Message &client_request, const struct sockaddr_in &client_addr, const tableClient &client_state, const tableAgregation &server_state)
{
     std::vector<ServerInfo> backups_to_notify;
    
 {
         lock_guard<mutex> lock(serverListMutex);
         if (server_list.size() <= 1) // Apenas o líder na lista
       
 {
             return true; // Não há backups para notificar.
        
 }
         backups_to_notify = this->server_list;
    
 }

     log_with_timestamp("[" + my_ip + "] [LEADER] Iniciando replicação para " + to_string(backups_to_notify.size() - 1) + " backup(s)...");
     int replication_socket = createSocket(0); // Usa uma porta efêmera.
     if (replication_socket == -1)
   
 {
         log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Falha ao criar socket para replicação.");
         return false;
    
 }
     setSocketTimeout(replication_socket, 2); // Timeout para esperar ACKs.
     
    // Monta a mensagem de replicação. Esta mensagem contém a "tabela atualizada".
    Message replication_msg = client_request;
     replication_msg.type = Type::REPLICATION_UPDATE;
     replication_msg.ip_addr = client_addr.sin_addr.s_addr; // IP do cliente original
     replication_msg.total_sum = client_state.last_sum; // Soma total do cliente
     replication_msg.total_reqs = client_state.last_req; // Requisições totais do cliente
     replication_msg.total_sum_server = server_state.sum; // Soma total do servidor
     replication_msg.total_reqs_server = server_state.num_reqs; // Requisições totais do servidor

     int successful_acks = 0;
     for (const auto &backup_info : backups_to_notify)
   
 {
         if (backup_info.ip_address == my_ip) // Não envia para si mesmo.
       
 {
             continue;
        
 }
         
        // Prepara o endereço do backup.
        struct sockaddr_in dest_addr = {};
         dest_addr.sin_family = AF_INET;
         dest_addr.sin_port = htons(this->server_communication_port);
         inet_pton(AF_INET, backup_info.ip_address.c_str(), &dest_addr.sin_addr);

         bool ack_received = false;
        
 // ============================================================================== //
 // ====> AQUI OCORRE O ENVIO DA TABELA ATUALIZADA (MENSAGEM DE REPLICAÇÃO) <==== //
 // A variável `replication_msg` contém todos os dados atualizados (estado do //
 // cliente e estado global do servidor) que precisam ser replicados no backup. //
 // ============================================================================== //
        sendto(replication_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

         // Espera pela confirmação (ACK) do backup.
        Message response;
         struct sockaddr_in from_addr;
         socklen_t from_len = sizeof(from_addr);
         if (recvfrom(replication_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
       
 {
             if (response.type == Type::REPLICATION_ACK && response.seq == client_request.seq)
           
 {
                 ack_received = true;
            
 }
        
 }

         if (ack_received)
       
 {
             successful_acks++;
             log_with_timestamp("[" + my_ip + "] [LEADER] ACK recebido de " + backup_info.ip_address);
        
 }
         else        
 {
             // TODO: Implementar lógica para remover backups que não respondem.
            log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Backup " + backup_info.ip_address + " não respondeu. Pode estar offline.");
        
 }
    
 }
     close(replication_socket);
     return true; // Na implementação atual, sempre retorna true.
}

/**
 * @brief (BACKUP) Aplica uma atualização de estado recebida do líder.
 * Atualiza as tabelas locais (participantes e soma global) para espelhar o estado do líder.
 * @param msg A mensagem de replicação recebida do líder.
 */
void Server::applyReplicationState(const Message &msg)
{
     struct in_addr client_addr_struct = {.s_addr = msg.ip_addr};
     string clientIP = inet_ntoa(client_addr_struct);
     
    // Atualiza o estado local com os dados da mensagem.
    updateParticipant(clientIP, msg.seq, msg.num);
     updateSumTable(msg.seq, msg.num);
     
    printParticipants(clientIP, "[BACKUP]");
}

/**
 * @brief Define o estado de um participante específico.
 * Usado principalmente durante a sincronização completa (state transfer).
 */
void Server::setParticipantState(const std::string &clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs)
{
     lock_guard<mutex> lock(participantsMutex);
     for (auto &p : participants)
   
 {
         if (p.address == clientIP)
       
 {
             p.last_sum = client_sum;
             p.last_req = client_reqs;
             p.last_value = value;
             return;
        
 }
    
 }
     participants.push_back({clientIP, client_reqs, client_sum, value});
}

/**
 * @brief Imprime o estado atualizado de um cliente e o estado global do servidor.
 */
void Server::printParticipants(const std::string &clientIP, const std::string &role_prefix)
{
     lock_guard<mutex> lock_participants(participantsMutex);
     lock_guard<mutex> lock_sum(sumMutex);
     for (const auto &p : participants)
   
 {
         if (p.address == clientIP)
       
 {
             string prefix = role_prefix.empty() ? "" : role_prefix + " ";
             string msg = prefix + "client " + p.address +
                         " id_req " + to_string(p.last_req) +
                         " value " + to_string(p.last_value) +
                         " num_reqs " + to_string(sumTotal.num_reqs) +
                         " total_sum " + to_string(sumTotal.sum);
             log_with_timestamp(msg);
             return;
        
 }
    
 }
}

/**
 * @brief Imprime uma mensagem indicando que uma requisição duplicada foi recebida.
 */
void Server::printRepet(const std::string &clientIP, uint32_t duplicate_seq)
{
     lock_guard<mutex> lock_participants(participantsMutex);
     lock_guard<mutex> lock_sum(sumMutex);
     for (const auto &p : participants)
   
 {
         if (p.address == clientIP)
       
 {
             string msg = " client " + p.address +
                         " DUP !! Tentativa de id_req " + to_string(duplicate_seq) +
                         ". Último id_req válido: " + to_string(p.last_req) +
                         " | num_reqs " + to_string(sumTotal.num_reqs) +
                         " total_sum " + to_string(sumTotal.sum);
             log_with_timestamp(msg);
             return;
        
 }
    
 }
}

/**
 * @brief Verifica se uma requisição de um cliente é duplicada (já foi processada).
 * @return true se o número de sequência da requisição for menor ou igual ao último processado.
 */
bool Server::isDuplicateRequest(const string &clientIP, uint32_t seq)
{
     lock_guard<mutex> lock(participantsMutex);
     for (const auto &p : participants)
   
 {
         if (p.address == clientIP)
       
 {
             return (seq <= p.last_req);
        
 }
    
 }
     return false; // Cliente não encontrado, então não é duplicado.
}

/**
 * @brief Verifica se um IP já existe na lista de servidores.
 * @return true se o IP já está na lista.
 */
bool Server::checkList(const std::string &ip)
{
     for (const auto &backup : server_list)
   
 {
         if (backup.ip_address == ip)
       
 {
             return true;
        
 }
    
 }
     return false;
}

/**
 * @brief Atualiza o estado de um participante (cliente) após uma nova requisição.
 * Se o participante não existir, ele é criado.
 * @return O estado atualizado do participante.
 */
tableClient Server::updateParticipant(const string &clientIP, uint32_t seq, uint32_t num)
{
     lock_guard<mutex> lock(participantsMutex);
     for (auto &p : participants)
   
 {
         if (p.address == clientIP)
       
 {
             p.last_sum += num;
             p.last_req = seq;
             p.last_value = num;
             return p;
        
 }
    
 }
     // Cria um novo participante se não for encontrado.
    tableClient new_participant = {clientIP, seq, num, num};
     participants.push_back(new_participant);
     return new_participant;
}

/**
 * @brief Atualiza a tabela de agregação global (soma total e número de requisições).
 */
void Server::updateSumTable(uint32_t seq, uint64_t num)
{
     lock_guard<mutex> lock(sumMutex);
     sumTotal.num_reqs++;
     sumTotal.sum += num;
}

/**
 * @brief Ponto de entrada do programa.
 * Analisa os argumentos da linha de comando, cria e inicia a instância do servidor.
 */
int main(int argc, char *argv[])
{
     if (argc < 2)
   
 {
         cerr << "Uso: " << argv[0] << " <porta_descoberta_cliente>" << endl;
         return 1;
    
 }
     
    // Define as portas com base em uma porta base fornecida como argumento.
    int client_disc_port = atoi(argv[1]);
     int client_req_port = client_disc_port + 1;
     int server_comm_port = client_disc_port + 2;
     
    // Cria e inicia o servidor.
    Server server(client_disc_port, client_req_port, server_comm_port);
     server.start();
     
    return 0;
}
