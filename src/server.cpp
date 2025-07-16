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

void log_with_timestamp(const string &message)
{
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}

long long int numreqs = 0;


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

void Server::start()
{
    findLeaderOrCreateGroup(); 

    while (true)
    {
        if (this->role == ServerRole::LEADER)
        {
            runAsLeader();
        }
        else
        { 
            runAsBackup();
        }

        log_with_timestamp("[" + my_ip + "] Transição de papel detectada. Reavaliando o estado...");
        this_thread::sleep_for(chrono::milliseconds(100)); 
    }
}

void Server::handleClientDiscovery(const struct sockaddr_in &fromAddr)
{
    string clientIP = inet_ntoa(fromAddr.sin_addr);

    Message response = {Type::DESC_ACK, 0, 0};
    sendto(this->client_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

    lock_guard<mutex> lock(participantsMutex);
    if (!checkList(clientIP))
    {
        participants.push_back({clientIP, 0, 0, 0});
    }
}

void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr)
{
    //log_with_timestamp("[" + my_ip + "] Recebi uma mensagem de SERVER_DISCOVERY de " + inet_ntoa(fromAddr.sin_addr));
    string new_server_ip = inet_ntoa(fromAddr.sin_addr);
    const int ACK_ATTEMPTS = 3;
    Message response = {Type::SERVER_DISCOVERY_ACK, 0, 0};

    for (int i = 0; i < ACK_ATTEMPTS; ++i)
    {
        //log_with_timestamp("[" + my_ip + "] Enviando SERVER_DISCOVERY_ACK para " + new_server_ip);
        sendto(this->server_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    lock_guard<mutex> lock(serverListMutex);
    if (!checkList(new_server_ip))
    {
        server_list.push_back({new_server_ip});
        log_with_timestamp("[" + my_ip + "] Servidor " + new_server_ip + " adicionado à lista de backups.");
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
    //log_with_timestamp("[" + my_ip + "] Recebi uma mensagem de ELECTION de " + challenger_ip);

    if (ipToInt(this->my_ip) > ipToInt(challenger_ip))
    {
        //log_with_timestamp("[" + my_ip + "] Meu IP é maior. Enviando rajada de OK_ANSWER e iniciando minha eleição.");
        Message answer_msg = {Type::OK_ANSWER, 0, 0};

        for (int i = 0; i < 3; ++i)
        {
            sendto(this->server_socket, &answer_msg, sizeof(answer_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
            this_thread::sleep_for(chrono::milliseconds(20));
        }

        startElection(); 
    }
}

void Server::handleCoordinatorMessage(const struct sockaddr_in &fromAddr)
{
    //log_with_timestamp("[" + my_ip + "] Recebi uma mensagem de COORDINATOR de " + inet_ntoa(fromAddr.sin_addr));
    this->leader_ip = inet_ntoa(fromAddr.sin_addr);
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
    setSocketTimeout(this->server_socket, 2);

    const int DISCOVERY_DURATION_SEC = 5;
    auto discovery_start_time = chrono::steady_clock::now();

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);

    Message discovery_msg = {Type::SERVER_DISCOVERY, 0, 0};

    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - discovery_start_time).count() < DISCOVERY_DURATION_SEC)
    {
        this_thread::sleep_for(chrono::milliseconds(100));
        //log_with_timestamp("[" + my_ip + "] Enviando mensagem de descoberta de servidor...");
        sendto(this->server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);
        //log_with_timestamp("[" + my_ip + "] Recebido " + to_string(received) + " bytes de resposta.");
        
        if (received > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
            continue;
            
            if (response.type == Type::SERVER_DISCOVERY_ACK)
            {
                this->leader_ip = from_ip;
                this->role = ServerRole::BACKUP;
                log_with_timestamp("[" + my_ip + "] Líder existente encontrado em " + this->leader_ip + ". Tornando-me BACKUP.");
                return;
            }
        }
    }
    log_with_timestamp("[" + my_ip + "] Nenhum líder encontrado. Iniciando processo de eleição.");
    startElection();
}

void Server::startElection()
{
    if (this->election_in_progress)
    {
        return;
    }

    this->election_in_progress = true;

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    broadcast_addr.sin_addr.s_addr = inet_addr(BROADCAST_ADDR);
    Message election_msg = {Type::ELECTION, 0, 0};
    sendto(this->server_socket, &election_msg, sizeof(election_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

    bool got_objection = false; 
    const int ELECTION_TIMEOUT_SEC = 5;
    auto election_start_time = chrono::steady_clock::now();


    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - election_start_time).count() < ELECTION_TIMEOUT_SEC)
    {

        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int received = recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len);

        if (received <= 0)
        {
            continue; 
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
            return; 

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
        //log_with_timestamp("[" + my_ip + "] Processo de eleição encerrado. ");
    }

    this->election_in_progress = false;
}

void Server::runAsLeader()
{
    log_with_timestamp("[" + my_ip + "] SOU O NOVO LIDER ");
    
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

    
    thread server_listener_thread(&Server::listenForServerMessages, this);
    thread client_listener_thread(&Server::listenForClientMessages, this);
    thread client_comm_thread(&Server::receiveNumbers, this);
    //thread heartbeat_thread(&Server::sendHeartbeats, this);

    while (this->role == ServerRole::LEADER)
    {
        this_thread::sleep_for(chrono::seconds(1));
    }

    server_listener_thread.join();
    client_listener_thread.join();
    client_comm_thread.join();
    //heartbeat_thread.join();
}

void Server::listenForServerMessages()
{
    while (this->role == ServerRole::LEADER)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;

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
    const int HEARTBEAT_TIMEOUT = 2; // Tempo sem heartbeat para iniciar um desafio
    const int CHALLENGE_TIMEOUT = 1; // Tempo para esperar a resposta do desafio

    while (this->role == ServerRole::BACKUP)
    {
        if (this->election_in_progress)
        {
            continue;
        }

        auto time_since_last_hb = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count();

        if (time_since_last_hb > HEARTBEAT_TIMEOUT)
        {

            struct sockaddr_in leader_addr = {};
            leader_addr.sin_family = AF_INET;
            leader_addr.sin_port = htons(this->server_communication_port);
            leader_addr.sin_addr.s_addr = inet_addr(this->leader_ip.c_str());

            Message challenge_msg = {Type::ARE_YOU_ALIVE, 0, 0};
            sendto(this->server_socket, &challenge_msg, sizeof(challenge_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));

            this_thread::sleep_for(chrono::seconds(CHALLENGE_TIMEOUT));

            auto time_after_challenge = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count();
            if (time_after_challenge > HEARTBEAT_TIMEOUT)
            {
                startElection();
            }

            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

void Server::printInicio()
{
    log_with_timestamp("num_reqs 0 total_sum 0");
}

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
            
            if (isDuplicateRequest(clientIP, number.seq)) {
            } else {
              
                tableClient clientState = updateParticipant(clientIP, number.seq, number.num);
                updateSumTable(number.seq, number.num);
                printParticipants(clientIP);
                tableAgregation server_state_copy;
                {
                    lock_guard<mutex> lock(sumMutex);
                    server_state_copy = this->sumTotal;
                }
                replicateToBackups(number, clientAddr, clientState, server_state_copy);
                Message confirmation;
                confirmation.type = Type::REQ_ACK;
                confirmation.seq = number.seq;
                confirmation.total_sum = clientState.last_sum;
                confirmation.total_reqs = clientState.last_req;
                sendto(numSocket, &confirmation, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);

            }
        }
    }
    close(numSocket);
}

bool Server::replicateToBackups(const Message& client_request, const struct sockaddr_in& client_addr, const tableClient& client_state, const tableAgregation& server_state)
{
    std::vector<ServerInfo> backups_to_notify;
    {
        lock_guard<mutex> lock(serverListMutex);
        if (server_list.empty()) {
            cout << "nao tem ninguém" << endl;
            return true; // Sucesso, pois não há backups para replicar.
        }
        cout << "tem alguem hehehe" << endl;
        backups_to_notify = this->server_list;
    }

    log_with_timestamp("[" + my_ip + "] [LEADER] Iniciando replicação síncrona, um por um, para " + to_string(backups_to_notify.size()) + " backup(s)...");
    Message replication_msg = client_request;
    replication_msg.type = Type::REPLICATION_UPDATE;
    replication_msg.ip_addr = client_addr.sin_addr.s_addr;
    replication_msg.total_sum = client_state.last_sum;
    replication_msg.total_reqs = client_state.last_req;
    replication_msg.total_sum_server = server_state.sum;
    replication_msg.total_reqs_server = server_state.num_reqs;
    setSocketTimeout(this->server_socket, 2); // Timeout de 2 segundos por resposta

    int successful_acks = 0;
    for (const auto& backup_info : backups_to_notify) {
        log_with_timestamp("[" + my_ip + "] [LEADER] Replicando para o backup " + backup_info.ip_address + "...");

        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        dest_addr.sin_addr.s_addr = inet_addr(backup_info.ip_address.c_str());

        bool ack_received = false;
        const int MAX_ATTEMPTS = 1; 

        for (int attempt = 0; attempt < MAX_ATTEMPTS && !ack_received; ++attempt) {
            sendto(this->server_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            Message response;
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);

            if (recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr*)&from_addr, &from_len) > 0) {
                if (response.type == Type::REPLICATION_ACK &&
                    from_addr.sin_addr.s_addr == dest_addr.sin_addr.s_addr &&
                    response.seq == client_request.seq)
                {
                    ack_received = true;
                }
            } else {
                // recvfrom retornou -1 (timeout) ou erro
                log_with_timestamp("[" + my_ip + "] [LEADER] AVISO: Timeout esperando ACK de " + backup_info.ip_address + " (tentativa " + to_string(attempt + 1) + ")");
            }
        }

        if (ack_received) {
            successful_acks++;
            log_with_timestamp("[" + my_ip + "] [LEADER] ACK recebido de " + backup_info.ip_address);
        } else {
            log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Backup " + backup_info.ip_address + " não respondeu após " + to_string(MAX_ATTEMPTS) + " tentativas. Pode estar offline.");
        }
    } 
    setSocketTimeout(this->server_socket, 0);

    log_with_timestamp("[" + my_ip + "] [LEADER] Replicação concluída. Sucesso para " + to_string(successful_acks) + "/" + to_string(backups_to_notify.size()) + " backups.");

    return true; 
}

void Server::setParticipantState(const std::string& clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs)
{
    lock_guard<mutex> lock(participantsMutex);
    for (auto &p : participants) {
        if (p.address == clientIP) {
            p.last_sum = client_sum;
            p.last_req = client_reqs; // O backup agora armazena o mesmo que o líder
            p.last_value = value;
            return;
        }
    }
    participants.push_back({clientIP, client_reqs, client_sum, value});
}

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
                    const int ACK_BURST_COUNT = 3;
                    Message ack_msg = {Type::REPLICATION_ACK, 0, msg.seq};

                    for (int i = 0; i < ACK_BURST_COUNT; ++i) {
                        sendto(server_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr *)&from_addr, from_len);
                        this_thread::sleep_for(chrono::milliseconds(10));
                    }

                    struct in_addr original_client_addr;
                    original_client_addr.s_addr = msg.ip_addr;
                    string client_ip_str = inet_ntoa(original_client_addr);
                                    
                    setParticipantState(client_ip_str, msg.seq, msg.num, msg.total_sum, msg.total_reqs);
                    {
                        lock_guard<mutex> lock_sum(sumMutex);
                        this->sumTotal.sum = msg.total_sum_server;
                        this->sumTotal.num_reqs = msg.total_reqs_server;
                    }
                
                    cout << "[BACKUP] ";
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
            return;
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
    // std::cout << "[DEBUG] Verificando IP: " << ip << std::endl;
    // std::cout << "[DEBUG] Lista de SERVERS:" << std::endl;

    // // Primeiro, imprime toda a lista
    // for (const auto &backup : server_list)
    // {
    //     std::cout << " - " << backup.ip_address << std::endl;
    // }

    // Agora, verifica se o IP está na lista
    for (const auto &backup : server_list)
    {
        if (backup.ip_address == ip)
        {
            // std::cout << "[DEBUG] IP encontrado na lista." << std::endl;
            return true;
        }
    }

    // std::cout << "[DEBUG] IP não encontrado na lista." << std::endl;
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
            return p;
        }
    }
    
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