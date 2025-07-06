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
#include <bits/stdc++.h>
#include <vector>
#include <thread>
#include <fcntl.h> 

using namespace std;

mutex participantsMutex;
mutex sumMutex;
mutex serverListMutex;

// --- Função auxiliar de log com timestamp ---
void log_with_timestamp(const string &message)
{
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}

long long int numreqs = 0;
// --- Construtor e Destrutor ---

Server::Server(int client_port, int req_port, int server_comm_port)
{
    this->client_discovery_port = client_port;
    this->client_request_port = req_port;
    this->server_communication_port = server_comm_port;
    this->my_ip = getIP();
    this->server_socket = -1;
    this->client_socket = -1;
    this->election_in_progress = false;

    this->role = ServerRole::BACKUP;
}
Server::~Server()
{
    if (server_socket != -1)
        close(server_socket);
    if (client_socket != -1)
        close(client_socket);
}

// --- Ponto de Entrada Principal (Máquina de Estados) ---
void Server::start()
{
    findLeaderOrCreateGroup(); // Define o papel inicial

    /* Loop principal para a transição de BACKUP para LÍDER */
    while (true)
    {
        if (this->role == ServerRole::LEADER)
        {
            //se lider: 
            runAsLeader();
        }
        else
        { 
            // Se o papel for BACKUP.
            runAsBackup();
        }

        // Se runAsBackup retornar, significa que uma transição de papel ocorreu
        log_with_timestamp("[" + my_ip + "] Transição de papel detectada. Reavaliando o estado...");
        this_thread::sleep_for(chrono::milliseconds(100)); // evita busy-looping
    }
}

void Server::handleClientDiscovery(const struct sockaddr_in &fromAddr)
{
    string clientIP = inet_ntoa(fromAddr.sin_addr);
    cout << "[" << my_ip << "] Recebido pedido de descoberta de CLIENTE de " << clientIP << endl;

    Message response = {Type::DESC_ACK, 0, 0};
    sendto(this->client_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

    lock_guard<mutex> lock(participantsMutex);
    if (!checkList(clientIP))
    {
        participants.push_back({clientIP, 0, 0, 0});
        cout << "[" << my_ip << "] Novo cliente registrado: " << clientIP << endl;
    }
}

void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr)
{
    string new_server_ip = inet_ntoa(fromAddr.sin_addr);
    const int ACK_ATTEMPTS = 3;
    Message response = {Type::SERVER_DISCOVERY_ACK, 0, 0};

    for (int i = 0; i < ACK_ATTEMPTS; ++i)
    {
        sendto(this->server_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    lock_guard<mutex> lock(serverListMutex);
    bool exists = false;
    for (const auto &s : server_list)
    {
        if (s.ip_address == new_server_ip)
        {
            exists = true;
            break;
        }
    }
    if (!exists)
    {
        server_list.push_back({new_server_ip});
        cout << "[" << my_ip << "] Servidor " << new_server_ip << " adicionado à lista de backups." << endl;
    }
}

uint32_t ipToInt(const std::string &ipStr)
{
    struct in_addr ip_addr;
    inet_aton(ipStr.c_str(), &ip_addr);
    return ntohl(ip_addr.s_addr);
}

void Server::handleElectionMessage(const struct sockaddr_in &fromAddr)
{
    string challenger_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Recebi uma mensagem de ELECTION de " + challenger_ip);

    if (ipToInt(this->my_ip) > ipToInt(challenger_ip))
    {
        log_with_timestamp("[" + my_ip + "] Meu IP é maior. Enviando rajada de OK_ANSWER e iniciando minha eleição.");
        Message answer_msg = {Type::OK_ANSWER, 0, 0};

        // Envia uma rajada de OK_ANSWER para aumentar a chance de entrega
        for (int i = 0; i < 3; ++i)
        {
            sendto(this->server_socket, &answer_msg, sizeof(answer_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
            this_thread::sleep_for(chrono::milliseconds(20));
        }

        startElection(); // Eu sou o "valentão", então inicio minha eleição.
    }
}

void Server::handleCoordinatorMessage(const struct sockaddr_in &fromAddr)
{
    this->leader_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Fui informado que o novo líder é: " + this->leader_ip);
    this->election_in_progress = false;
    this->last_heartbeat_time = chrono::steady_clock::now();

    if (this->leader_ip == this->my_ip)
    {
        this->role = ServerRole::LEADER;
    }
}

void Server::findLeaderOrCreateGroup()
{
    cout << "[" << my_ip << "] Procurando por um líder na rede..." << endl;

    this->server_socket = createSocket(this->server_communication_port);
    if (this->server_socket == -1)
    {
        cerr << "Erro fatal ao criar socket de servidor." << endl;
        exit(1);
    }
    setSocketBroadcastOptions(this->server_socket);
    setSocketTimeout(this->server_socket, 1);

    const int DISCOVERY_DURATION_SEC = 5;
    auto discovery_start_time = chrono::steady_clock::now();

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);

    Message discovery_msg = {Type::SERVER_DISCOVERY, 0, 0};

    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - discovery_start_time).count() < DISCOVERY_DURATION_SEC)
    {
        sendto(this->server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);

        if (received > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;

            if (response.type == Type::SERVER_DISCOVERY_ACK)
            {
                this->leader_ip = from_ip;
                this->role = ServerRole::BACKUP;
                cout << "[" << my_ip << "] Líder existente encontrado em " << this->leader_ip << ". Tornando-me BACKUP." << endl;
                return;
            }
        }
    }

    cout << "[" << my_ip << "] Fase de descoberta encerrada. Nenhum líder respondeu. Iniciando uma eleição." << endl;
    startElection();
}

void Server::startElection()
{
    if (this->election_in_progress)
    {
        return;
    }

    this->election_in_progress = true;
    log_with_timestamp("[" + my_ip + "] Iniciando processo de eleição...");

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    Message election_msg = {Type::ELECTION, 0, 0};
    sendto(this->server_socket, &election_msg, sizeof(election_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

    bool got_objection = false; // Renomeado para maior clareza
    const int ELECTION_TIMEOUT_SEC = 5;
    log_with_timestamp("[" + my_ip + "] Aguardando respostas por até " + to_string(ELECTION_TIMEOUT_SEC) + " segundos...");
    auto election_start_time = chrono::steady_clock::now();


    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - election_start_time).count() < ELECTION_TIMEOUT_SEC)
    {

        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);

        if (received <= 0)
        {
            continue; // Timeout ou erro, apenas continua o loop de espera.
        }

        string from_ip = inet_ntoa(from_addr.sin_addr);
        if (from_ip == my_ip)
            continue;

        switch (response.type)
        {
        case Type::OK_ANSWER:
            got_objection = true;
            break;

        case Type::ELECTION:

            if (ipToInt(this->my_ip) > ipToInt(from_ip))
            {
                Message answer_msg = {Type::OK_ANSWER, 0, 0};
                //for (int i = 0; i < 3; ++i)
                {
                    sendto(this->server_socket, &answer_msg, sizeof(answer_msg), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));
                    this_thread::sleep_for(chrono::milliseconds(20));
                }
            }
            else
            {
                got_objection = true;
            }
            break;

        case Type::COORDINATOR:
            handleCoordinatorMessage(from_addr);
            // A flag election_in_progress será setada como false dentro de handleCoordinatorMessage
            return; // Encerra a função de eleição

        default:
            break;
        }

        if (got_objection)
        {
            break;
        }
    }

    if (!got_objection)
    {
        log_with_timestamp("[" + my_ip + "] Eleição vencida! Nenhuma objeção recebida. EU SOU O NOVO LÍDER!");
        this->role = ServerRole::LEADER;
        this->leader_ip = this->my_ip;

        const int ANNOUNCEMENT_ATTEMPTS = 3;
        Message coordinator_msg = {Type::COORDINATOR, 0, 0};
        for (int i = 0; i < ANNOUNCEMENT_ATTEMPTS; ++i)
        {
            sendto(this->server_socket, &coordinator_msg, sizeof(coordinator_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    }
    else
    {
        log_with_timestamp("[" + my_ip + "] Processo de eleição encerrado. ");
    }

    this->election_in_progress = false;
}

void Server::runAsLeader()
{
    // --- FASE DE REDESCOBERTA ---
    serverListMutex.lock();
    server_list.clear();
    serverListMutex.unlock();

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);

    Message announcement_msg = {Type::COORDINATOR, 0, 0};
    for (int i = 0; i < 3; ++i)
    {
        sendto(this->server_socket, &announcement_msg, sizeof(announcement_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::milliseconds(100));
    }

  /*  auto discovery_end_time = chrono::steady_clock::now() + chrono::seconds(3);
    while (chrono::steady_clock::now() < discovery_end_time)
    {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);
    }
*/
    // --- FASE DE OPERAÇÃO ---
    // Em vez de um loop com select, agora o líder apenas gerencia threads dedicadas.
    thread server_listener_thread(&Server::listenForServerMessages, this);
    thread client_listener_thread(&Server::listenForClientMessages, this);
    thread client_comm_thread(&Server::receiveNumbers, this);
    thread heartbeat_thread(&Server::sendHeartbeats, this);

    // O loop principal do líder agora apenas mantém o programa vivo
    // enquanto o seu papel for LÍDER.
    while (this->role == ServerRole::LEADER)
    {
        this_thread::sleep_for(chrono::seconds(1));
    }

    // terminam quando a condição `this->role == ServerRole::LEADER` falhar.
    server_listener_thread.join();
    client_listener_thread.join();
    client_comm_thread.join();
    heartbeat_thread.join();
}

void Server::listenForServerMessages()
{
    log_with_timestamp("[" + my_ip + "] Thread de escuta de SERVIDORES iniciada.");
    while (this->role == ServerRole::LEADER)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;

        // Chamada bloqueante no socket do servidor
        if (recvfrom(this->server_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            switch (msg.type)
            {
            case Type::SERVER_DISCOVERY:
                handleServerDiscovery(from_addr);
                break;

            case Type::ELECTION:
            {
                Message response_msg = {Type::COORDINATOR, 0, 0};
                sendto(this->server_socket, &response_msg, sizeof(response_msg), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));
            }
            break;

            case Type::ARE_YOU_ALIVE:
            {
                string backup_ip = inet_ntoa(from_addr.sin_addr);
                lock_guard<mutex> lock(serverListMutex);
                if (!checkList(backup_ip))
                {
                    server_list.push_back({backup_ip});
                }

                Message response_msg = {Type::I_AM_ALIVE, 0, 0};
                sendto(this->server_socket, &response_msg, sizeof(response_msg), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));
            }
            break;

            default:
                break;
            }
        }
    }
}

void Server::listenForClientMessages()
{

    // Cria um socket dedicado para esta thread
    this->client_socket = createSocket(this->client_discovery_port);
    if (this->client_socket == -1)
    {
        log_with_timestamp("[" + my_ip + "] LEADER: Falha ao criar socket de descoberta de cliente. Thread de escuta encerrando.");
        return;
    }

    while (this->role == ServerRole::LEADER)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;

        // Chamada bloqueante no socket do cliente
        if (recvfrom(this->client_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            if (msg.type == Type::DESC)
            {
                handleClientDiscovery(from_addr);
            }
        }
    }
    close(this->client_socket);
    this->client_socket = -1;
}

void Server::sendHeartbeats()
{
    int sock = createSocket(0);
    while (this->role == ServerRole::LEADER)
    {
        this_thread::sleep_for(chrono::seconds(2));
        lock_guard<mutex> lock(serverListMutex);
        Message hb_msg = {Type::HEARTBEAT, 0, 0, 0};

        for (const auto &server_info : server_list)
        {
            if (server_info.ip_address != this->my_ip)
            {
                struct sockaddr_in dest_addr = {};
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(this->server_communication_port);
                dest_addr.sin_addr.s_addr = inet_addr(server_info.ip_address.c_str());

                sendto(sock, &hb_msg, sizeof(hb_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            }
        }
    }
    close(sock);
}

void Server::checkForLeaderFailure()
{
    this->last_heartbeat_time = chrono::steady_clock::now();
    const int HEARTBEAT_TIMEOUT = 10; // Tempo sem heartbeat para iniciar um desafio
    const int CHALLENGE_TIMEOUT = 4; // Tempo para esperar a resposta do desafio

    while (this->role == ServerRole::BACKUP)
    {
        this_thread::sleep_for(chrono::seconds(1));

        // Não faz nada se uma eleição já está acontecendo
        if (this->election_in_progress)
        {
            continue;
        }

        auto time_since_last_hb = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count();

        // Se o timeout do heartbeat foi atingido, DESAFIE o líder
        if (time_since_last_hb > HEARTBEAT_TIMEOUT)
        {

            // Envia uma mensagem direta de desafio ao líder
            struct sockaddr_in leader_addr = {};
            leader_addr.sin_family = AF_INET;
            leader_addr.sin_port = htons(this->server_communication_port);
            leader_addr.sin_addr.s_addr = inet_addr(this->leader_ip.c_str());

            Message challenge_msg = {Type::ARE_YOU_ALIVE, 0, 0};
            sendto(this->server_socket, &challenge_msg, sizeof(challenge_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));

            // Aguarda por um curto período pela resposta I_AM_ALIVE
            this_thread::sleep_for(chrono::seconds(CHALLENGE_TIMEOUT));

            // Verifica novamente. Se o tempo não foi resetado, o líder está morto.
            auto time_after_challenge = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count();
            if (time_after_challenge > HEARTBEAT_TIMEOUT)
            {
                startElection();
            }
            else
            {
            }
        }
    }
}

void Server::printInicio()
{
    log_with_timestamp("num_reqs 0 total_sum 0");
}

// Thread do líder que processa os pedidos dos clientes e gere a replicação
void Server::receiveNumbers() {
    int numSocket = createSocket(this->client_request_port);
    if (numSocket == -1) return;
    

    while (this->role == ServerRole::LEADER) {
        Message number;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int received = recvfrom(numSocket, &number, sizeof(Message), 0, (struct sockaddr*)&clientAddr, &clientLen);
        
        if (received > 0 && number.type == Type::REQ) {
            string clientIP = inet_ntoa(clientAddr.sin_addr);
            
            Message confirmation;
            confirmation.type = Type::REQ_ACK;
            confirmation.seq = number.seq;

            if (isDuplicateRequest(clientIP, number.seq)) {
                printRepet(clientIP, number.seq);
                // Se for duplicado, ainda precisamos de enviar o estado mais recente
                lock_guard<mutex> lock(participantsMutex);
                for (const auto& p : participants) {
                    if (p.address == clientIP) {
                        confirmation.total_sum = p.last_sum;
                        confirmation.total_reqs = p.last_req;
                        sendto(numSocket, &confirmation, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);
                        break;
                    }
                }
            } else {
                // 1. Atualiza o participante e obtém o estado mais recente de volta numa só operação
                tableClient clientState = updateParticipant(clientIP, number.seq, number.num);
                
                // 2. Atualiza a soma global (operação separada)
                updateSumTable(number.seq, number.num);
                
                // 3. Imprime o estado local do servidor usando o IP
                printParticipants(clientIP);

                // 4. Preenche a confirmação com o estado individual do cliente que foi retornado
                confirmation.total_sum = clientState.last_sum;
                confirmation.total_reqs = clientState.last_req;
                sendto(numSocket, &confirmation, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);

                replicateToBackups(number, clientAddr);
                
            }
            
        }
    }
    close(numSocket);
}

// Função para gerir a replicação passiva (com mais logs para depuração)
bool Server::replicateToBackups(const Message &client_request, const struct sockaddr_in &client_addr)
{
    lock_guard<mutex> lock(serverListMutex);
    if (server_list.empty())
    {
        return true;
    }

    int backups_count = server_list.size();
    atomic<int> acks_received(0);

    Message replication_msg = client_request;
    replication_msg.type = Type::REPLICATION_UPDATE;
    replication_msg.ip_addr = client_addr.sin_addr.s_addr;

    for (const auto &backup_info : server_list)
    {
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        dest_addr.sin_addr.s_addr = inet_addr(backup_info.ip_address.c_str());
        sendto(this->server_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
    //todo verificar tempo d timeout
    int flags = fcntl(this->server_socket, F_GETFL, 0);
    fcntl(this->server_socket, F_SETFL, flags | O_NONBLOCK); // Torna o socket não bloqueante
    auto end_time = chrono::steady_clock::now() + chrono::seconds(2); 

    while(chrono::steady_clock::now() < end_time && acks_received < backups_count) {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        auto received = recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr*)&from_addr, &from_len);
        if(received > 0) {
            if (response.type == Type::REPLICATION_ACK && response.seq == replication_msg.seq) {
                acks_received++;
        
            }
        }
        else if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            this_thread::sleep_for(std::chrono::milliseconds(10)); // Pequeno sleep para não queimar CPU
        }
    }
    fcntl(this->server_socket, F_SETFL, flags);
    return acks_received == backups_count;
}

// --- Lógica do BACKUP ---
void Server::runAsBackup()
{
    thread failure_detection_thread(&Server::checkForLeaderFailure, this);
    log_with_timestamp("[" + my_ip + "] Papel de BACKUP assumido. Aguardando mensagens do líder (" + this->leader_ip + ").");

    while (this->role == ServerRole::BACKUP)
    {
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int received = recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len);
        if (received > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == this->leader_ip)
            {
                this->last_heartbeat_time = chrono::steady_clock::now();
            }
            switch (msg.type)
            {
            case Type::HEARTBEAT:
            case Type::I_AM_ALIVE:
                break;
            case Type::REPLICATION_UPDATE:
                if (from_ip == this->leader_ip)
                {
                    // Envia confirmação de volta ao líder
                    Message ack_msg = {Type::REPLICATION_ACK, 0, msg.seq};
                    sendto(server_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr *)&from_addr, from_len);

                    struct in_addr original_client_addr;
                    original_client_addr.s_addr = msg.ip_addr;
                    string client_ip_str = inet_ntoa(original_client_addr);

                    log_with_timestamp("[" + my_ip + "] [BACKUP] Recebida atualização de estado do líder para o cliente " + client_ip_str);

                    updateParticipant(client_ip_str, msg.seq, msg.num);
                    updateSumTable(msg.seq, msg.num);

                    // Imprime o estado atualizado do backup para confirmar a replicação.
                    printParticipants(client_ip_str);
                    
                }
                break;
            case Type::ELECTION:
                handleElectionMessage(from_addr);
                break;
            case Type::COORDINATOR:
                handleCoordinatorMessage(from_addr);
                break;
            default:
                break;
            }
        }
    }
    failure_detection_thread.join();
}

void Server::printParticipants(const std::string &clientIP)
{
    lock_guard<mutex> lock_participants(participantsMutex);
    lock_guard<mutex> lock_sum(sumMutex);

    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            string msg = " client " + p.address +
                         " id_req " + to_string(p.last_req) +
                         " value " + to_string(p.last_value) +
                         " num_reqs " + to_string(sumTotal.num_reqs) +
                         " total_sum " + to_string(sumTotal.sum);
            log_with_timestamp(msg);
            return; // Encontrou e imprimiu, pode sair
        }
    }
}

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

bool Server::isDuplicateRequest(const string &clientIP, uint32_t seq)
{
    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            return (p.last_req == seq);
        }
    }
    return false;
}

bool Server::checkList(const std::string &ip)
{
    for (const auto &participant : participants)
    {
        if (participant.address == ip)
        {
            return true;
        }
    }
    return false;
}

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
            return p; // Retorna a estrutura atualizada do participante encontrado
        }
    }
    
    // Se o participante não for encontrado, cria um novo e retorna-o
    tableClient new_participant = {clientIP, seq, num, num};
    participants.push_back(new_participant);
    return new_participant;
}

void Server::updateSumTable(uint32_t seq, uint64_t num)
{
    sumTotal.num_reqs++;
    sumTotal.sum += num;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Uso: " << argv[0] << " <porta_descoberta_cliente>" << endl;
        return 1;
    }

    int client_disc_port = atoi(argv[1]);
    int client_req_port = client_disc_port + 1;
    int server_comm_port = client_disc_port + 2;

    Server server(client_disc_port, client_req_port, server_comm_port);
    server.start();

    return 0;
}