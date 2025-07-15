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
#include <atomic>
#include <random> // ALTERAÇÃO: Melhor gerador de números aleatórios
#include <string>
#include <algorithm>

using namespace std;

mutex participantsMutex;
mutex sumMutex;
mutex serverListMutex;

void log_with_timestamp(const string &message) {
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}

Server::Server(int client_port, int req_port, int server_comm_port) {
    this->client_discovery_port = client_port;
    this->client_request_port = req_port;
    this->server_communication_port = server_comm_port;
    this->my_ip = getIP();
    this->server_socket = -1;
    this->client_socket = -1;
    this->current_state = ServerState::NORMAL; 
    this->role = ServerRole::BACKUP;
}

Server::~Server() {
    if (server_socket != -1) close(server_socket);
    if (client_socket != -1) close(client_socket);
}

void Server::start() {
    findLeaderOrCreateGroup(); 
    while (true) {
        if (this->role == ServerRole::LEADER) {
            runAsLeader();
        } else { 
            runAsBackup();
        }
        log_with_timestamp("[" + my_ip + "] Fim de um ciclo de papel. Reavaliando o estado...");
        this_thread::sleep_for(chrono::milliseconds(100)); 
    }
}

void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr) {
    string new_server_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Recebi SERVER_DISCOVERY de " + new_server_ip);
    
    Message response = {Type::SERVER_DISCOVERY_ACK, 0, 0};
    sendto(this->server_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

    lock_guard<mutex> lock(serverListMutex);
    if (!checkList(new_server_ip)) {
        server_list.push_back({new_server_ip});
        log_with_timestamp("[" + my_ip + "] Servidor " + new_server_ip + " adicionado à lista de backups.");
    }
}

uint32_t ipToInt(const std::string &ipStr) {
    struct in_addr ip_addr;
    inet_aton(ipStr.c_str(), &ip_addr);
    return ntohl(ip_addr.s_addr);
}

void Server::handleElectionMessage(const struct sockaddr_in &fromAddr) {
    string challenger_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Recebi ELECTION de " + challenger_ip);

    if (ipToInt(this->my_ip) > ipToInt(challenger_ip)) {
        log_with_timestamp("[" + my_ip + "] Meu IP é maior. Enviando OK_ANSWER.");
        Message answer_msg = {Type::OK_ANSWER, 0, 0};
        sendto(this->server_socket, &answer_msg, sizeof(answer_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
        startElection(); 
    }
}

void Server::handleCoordinatorMessage(const struct sockaddr_in &fromAddr) {
    string new_leader_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Recebi COORDINATOR de " + new_leader_ip);
    this->leader_ip = new_leader_ip;
    this->current_state = ServerState::NORMAL; 
    this->last_heartbeat_time = chrono::steady_clock::now();

    if (this->leader_ip == this->my_ip) {
        this->role = ServerRole::LEADER;
    } else {
        this->role = ServerRole::BACKUP;
    }
}

void Server::findLeaderOrCreateGroup() {
    log_with_timestamp("[" + my_ip + "] Procurando por um líder na rede...");

    // ALTERAÇÃO: Adicionado atraso aleatório para evitar "tempestade de eleições" na inicialização.
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> distrib(100, 500);
    this_thread::sleep_for(chrono::milliseconds(distrib(gen)));

    this->server_socket = createSocket(this->server_communication_port);
    if (this->server_socket == -1) {
        log_with_timestamp("Erro fatal ao criar socket de servidor.");
        exit(1);
    }
    setSocketBroadcastOptions(this->server_socket);
    setSocketTimeout(this->server_socket, 2);

    const int DISCOVERY_DURATION_SEC = 3;
    auto discovery_start_time = chrono::steady_clock::now();

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
    Message discovery_msg = {Type::SERVER_DISCOVERY, 0, 0};
    
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - discovery_start_time).count() < DISCOVERY_DURATION_SEC) {
        sendto(this->server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip != my_ip && response.type == Type::SERVER_DISCOVERY_ACK) {
                 this->leader_ip = from_ip;
                 this->role = ServerRole::BACKUP;
                 log_with_timestamp("[" + my_ip + "] Líder existente encontrado em " + this->leader_ip + ". Tornando-me BACKUP.");
                 setSocketTimeout(this->server_socket, 0);
                 return;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(500));
    }
    
    log_with_timestamp("[" + my_ip + "] Nenhum líder encontrado. Iniciando processo de eleição.");
    setSocketTimeout(this->server_socket, 0);
    startElection();
}

void Server::startElection() {
    ServerState expected = ServerState::NORMAL;
    if (!this->current_state.compare_exchange_strong(expected, ServerState::ELECTION_RUNNING)) {
        log_with_timestamp("[" + my_ip + "] Tentei iniciar eleição, mas uma já está em andamento.");
        return;
    }

    log_with_timestamp("[" + my_ip + "] INICIANDO ELEIÇÃO.");
    
    // 1. Pega uma cópia da lista de servidores para trabalhar
    std::vector<ServerInfo> current_servers;
    {
        lock_guard<mutex> lock(serverListMutex);
        current_servers = this->server_list;
    }

    // 2. Envia mensagem ELECTION (UNICAST) apenas para servidores com IP maior
    bool has_bullies = false;
    for (const auto& server : current_servers) {
        if (ipToInt(server.ip_address) > ipToInt(this->my_ip)) {
            has_bullies = true;
            log_with_timestamp("[" + my_ip + "] Enviando ELECTION para o valentão: " + server.ip_address);
            
            struct sockaddr_in dest_addr = {};
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(this->server_communication_port);
            inet_pton(AF_INET, server.ip_address.c_str(), &dest_addr.sin_addr);
            
            Message election_msg = {Type::ELECTION, 0, 0};
            sendto(this->server_socket, &election_msg, sizeof(election_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }
    }

    // 3. Se não existem valentões, declara-se líder imediatamente.
    if (!has_bullies) {
        log_with_timestamp("[" + my_ip + "] Nenhum servidor com IP maior encontrado. Venci a eleição instantaneamente.");
        this->role = ServerRole::LEADER;
        this->leader_ip = this->my_ip;
        this->current_state = ServerState::NORMAL;

        struct sockaddr_in broadcast_addr = {};
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
        Message coordinator_msg = {Type::COORDINATOR, 0, 0};
        sendto(this->server_socket, &coordinator_msg, sizeof(coordinator_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        return;
    }

    // 4. Se enviou mensagens para valentões, espera por uma resposta.
    bool got_objection = false; 
    const int ELECTION_TIMEOUT_SEC = 4; // Usando o timeout maior
    auto election_start_time = chrono::steady_clock::now();
    setSocketTimeout(this->server_socket, ELECTION_TIMEOUT_SEC);

    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - election_start_time).count() < ELECTION_TIMEOUT_SEC) {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip) continue;

            if (response.type == Type::OK_ANSWER) {
                log_with_timestamp("[" + my_ip + "] Recebi OK_ANSWER de " + from_ip + ". Perdi a eleição.");
                got_objection = true;
                break;
            } else if (response.type == Type::COORDINATOR) {
                log_with_timestamp("[" + my_ip + "] Recebi um COORDINATOR durante minha eleição. Abortando.");
                handleCoordinatorMessage(from_addr);
                setSocketTimeout(this->server_socket, 0);
                return; // A eleição acabou, um líder já foi eleito.
            }
        }
    }
    setSocketTimeout(this->server_socket, 0);

    // 5. Se o timeout estourou sem objeções, declara-se líder.
    if (!got_objection) {
        log_with_timestamp("[" + my_ip + "] Valentões não responderam. Venci a eleição.");
        this->role = ServerRole::LEADER;
        this->leader_ip = this->my_ip;
        this->current_state = ServerState::NORMAL;
        
        struct sockaddr_in broadcast_addr = {};
        broadcast_addr.sin_family = AF_INET;
        broadcast_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
        Message coordinator_msg = {Type::COORDINATOR, 0, 0};
        sendto(this->server_socket, &coordinator_msg, sizeof(coordinator_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
    } else {
        waitForNewLeader();
    }
}

// ALTERAÇÃO: Nova função para aguardar o anúncio do novo líder e evitar loops.
void Server::waitForNewLeader() {
    log_with_timestamp("[" + my_ip + "] Aguardando anúncio do novo coordenador...");
    this->current_state = ServerState::WAITING_FOR_COORDINATOR;
    const int WAIT_FOR_LEADER_TIMEOUT_SEC = 5;
    setSocketTimeout(this->server_socket, WAIT_FOR_LEADER_TIMEOUT_SEC);

    Message msg;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    if (recvfrom(this->server_socket, &msg, sizeof(msg), 0, (struct sockaddr*)&from_addr, &from_len) > 0) {
        if (msg.type == Type::COORDINATOR) {
            handleCoordinatorMessage(from_addr);
        }
    } else {
        log_with_timestamp("[" + my_ip + "] Timeout! Nenhum coordenador se anunciou. Iniciando nova eleição.");
        this->current_state = ServerState::NORMAL;
        startElection();
    }
    setSocketTimeout(this->server_socket, 0);
}

// Funções de Papel (Leader, Backup)
void Server::runAsLeader() {
    log_with_timestamp("ASSUMINDO PAPEL DE LÍDER.");
    
    // ======================================================================
    // CORREÇÃO #1: NÃO limpar a lista de servidores aqui.
    // Os servidores descobertos durante a eleição precisam ser mantidos.
    // ======================================================================
    // serverListMutex.lock();
    // server_list.clear();
    // serverListMutex.unlock();

    if (this->server_socket == -1) this->server_socket = createSocket(this->server_communication_port);
    if (this->client_socket == -1) this->client_socket = createSocket(this->client_discovery_port);

    thread server_listener_thread(&Server::listenForServerMessages, this);
    thread client_listener_thread(&Server::listenForClientMessages, this);
    thread client_comm_thread(&Server::receiveNumbers, this);
    thread heartbeat_thread(&Server::sendHeartbeats, this);

    while (this->role == ServerRole::LEADER) {
        this_thread::sleep_for(chrono::seconds(1));
    }
    log_with_timestamp("Deixando o papel de líder.");
    
    server_listener_thread.join();
    client_listener_thread.join();
    client_comm_thread.join();
    heartbeat_thread.join();
}

void Server::listenForServerMessages() {
    while (this->role == ServerRole::LEADER) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;
        setSocketTimeout(this->server_socket, 1);
        if (recvfrom(this->server_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if(from_ip == my_ip) continue;

            switch (msg.type) {
                case Type::SERVER_DISCOVERY:   
                    handleServerDiscovery(from_addr);
                    break;
                case Type::ELECTION: {
                    log_with_timestamp("["+my_ip+"] Recebi ELECTION de "+from_ip+", respondendo que sou o líder.");
                    Message response_msg = {Type::COORDINATOR, 0, 0};
                    sendto(this->server_socket, &response_msg, sizeof(response_msg), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));
                    break;
                }
                case Type::ARE_YOU_ALIVE: {
                    Message response_msg = {Type::I_AM_ALIVE, 0, 0};
                    sendto(this->server_socket, &response_msg, sizeof(response_msg), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void Server::listenForClientMessages() {
    if (this->client_socket == -1) this->client_socket = createSocket(this->client_discovery_port);
    if (this->client_socket == -1) {
        log_with_timestamp("[" + my_ip + "] LEADER: Falha ao criar socket de descoberta de cliente.");
        return;
    }
    while (this->role == ServerRole::LEADER) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;
        setSocketTimeout(this->client_socket, 1);
        if (recvfrom(this->client_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            if (msg.type == Type::DESC) {
                handleClientDiscovery(from_addr);
            }
        }
    }
    close(this->client_socket);
    this->client_socket = -1;
}

void Server::sendHeartbeats() {
    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
    setSocketBroadcastOptions(this->server_socket);
    Message hb_msg = {Type::HEARTBEAT, 0, 0, 0};

    while (this->role == ServerRole::LEADER) {
        this_thread::sleep_for(chrono::seconds(2));
        sendto(this->server_socket, &hb_msg, sizeof(hb_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
    }
}

void Server::checkForLeaderFailure() {
    this->last_heartbeat_time = chrono::steady_clock::now();
    const int HEARTBEAT_TIMEOUT_SEC = 5;
    const int CHALLENGE_TIMEOUT_MSEC = 1000;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> distrib(50, 250);

    while (this->role == ServerRole::BACKUP) {
        this_thread::sleep_for(chrono::seconds(1));
        if (this->current_state != ServerState::NORMAL) continue;

        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count() > HEARTBEAT_TIMEOUT_SEC) {
            log_with_timestamp("[" + my_ip + "] Líder (" + leader_ip + ") inativo. Desafiando...");
            
            struct sockaddr_in leader_addr = {};
            leader_addr.sin_family = AF_INET;
            leader_addr.sin_port = htons(this->server_communication_port);
            inet_pton(AF_INET, this->leader_ip.c_str(), &leader_addr.sin_addr);

            Message challenge_msg = {Type::ARE_YOU_ALIVE, 0, 0};
            sendto(this->server_socket, &challenge_msg, sizeof(challenge_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));
            this_thread::sleep_for(chrono::milliseconds(CHALLENGE_TIMEOUT_MSEC));

            if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count() > HEARTBEAT_TIMEOUT_SEC) {
                log_with_timestamp("[" + my_ip + "] Desafio não respondido. Líder considerado morto.");
                
                // ======================================================================
                // CORREÇÃO FINAL: Atraso aleatório antes de iniciar a eleição
                // para quebrar a simetria entre os backups.
                // ======================================================================
                this_thread::sleep_for(chrono::milliseconds(distrib(gen)));
                startElection();
            }
        }
    }
}
void Server::runAsBackup() {
    log_with_timestamp("[" + my_ip + "] Atuando como BACKUP. Líder: " + this->leader_ip);
    thread failure_detection_thread(&Server::checkForLeaderFailure, this);
    setSocketTimeout(this->server_socket, 0);

    while (this->role == ServerRole::BACKUP) {
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);

            if (from_ip == this->leader_ip) this->last_heartbeat_time = chrono::steady_clock::now();
            
            switch (msg.type) {
                case Type::HEARTBEAT:
                case Type::I_AM_ALIVE:
                     if(from_ip == this->leader_ip) {
                        this->last_heartbeat_time = chrono::steady_clock::now();
                     }
                     break;
                // ======================================================================
                // CORREÇÃO #2: Implementar o case de REPLICATION_UPDATE
                // ======================================================================
                case Type::REPLICATION_UPDATE:
                    if (from_ip == this->leader_ip) {
                        log_with_timestamp("[" + my_ip + "] Recebi REPLICATION_UPDATE do líder.");
                        
                        // Atualiza o estado interno
                        struct in_addr original_client_addr;
                        original_client_addr.s_addr = msg.ip_addr;
                        string client_ip_str = inet_ntoa(original_client_addr);
                        setParticipantState(client_ip_str, msg.seq, msg.num, msg.total_sum, msg.total_reqs);
                        {
                            lock_guard<mutex> lock_sum(sumMutex);
                            this->sumTotal.sum = msg.total_sum_server;
                            this->sumTotal.num_reqs = msg.total_reqs_server;
                        }
                        
                        // Envia o ACK de volta para o líder
                        Message ack_msg = {Type::REPLICATION_ACK, 0, msg.seq};
                        sendto(server_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr *)&from_addr, from_len);
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

// Funções restantes (utilitárias, de impressão e de negócio)
void Server::receiveNumbers() {
    int numSocket = createSocket(this->client_request_port);
    if (numSocket == -1) return;
    
    while (this->role == ServerRole::LEADER) {
        Message number;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        setSocketTimeout(numSocket, 1);
        int received = recvfrom(numSocket, &number, sizeof(Message), 0, (struct sockaddr*)&clientAddr, &clientLen);
        
        if (received > 0 && number.type == Type::REQ) {
            string clientIP = inet_ntoa(clientAddr.sin_addr);
            
            if (isDuplicateRequest(clientIP, number.seq)) {
                printRepet(clientIP, number.seq);
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
                Message confirmation = {Type::REQ_ACK, number.seq, clientState.last_req, clientState.last_sum};
                sendto(numSocket, &confirmation, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);
            }
        }
    }
    close(numSocket);
}

// As funções abaixo não precisaram de alterações na lógica principal de eleição
void Server::printInicio() { log_with_timestamp("num_reqs 0 total_sum 0"); }
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

    int replication_socket = createSocket(0); // Cria um socket sem bind, apenas para enviar/receber.
    if (replication_socket == -1) {
        log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Falha ao criar socket para replicação.");
        return false;
    }
    setSocketTimeout(replication_socket, 2); // Timeout de 2 segundos para cada resposta de backup.
 
    
    Message replication_msg = client_request;
    replication_msg.type = Type::REPLICATION_UPDATE;
    replication_msg.ip_addr = client_addr.sin_addr.s_addr;
    replication_msg.total_sum = client_state.last_sum;
    replication_msg.total_reqs = client_state.last_req;
    replication_msg.total_sum_server = server_state.sum;
    replication_msg.total_reqs_server = server_state.num_reqs;
    //setSocketTimeout(this->server_socket, 2); // Timeout de 2 segundos por resposta

    int successful_acks = 0;
    for (const auto& backup_info : backups_to_notify) {
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, backup_info.ip_address.c_str(), &dest_addr.sin_addr);

        bool ack_received = false;
        // Envia a mensagem de replicação
        sendto(replication_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

        // Tenta receber o ACK
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(replication_socket, &response, sizeof(response), 0, (struct sockaddr*)&from_addr, &from_len) > 0) {
            if (response.type == Type::REPLICATION_ACK && response.seq == client_request.seq) {
                ack_received = true;
            }
        }

        if (ack_received) {
            successful_acks++;
            log_with_timestamp("[" + my_ip + "] [LEADER] ACK recebido de " + backup_info.ip_address);
        } else {
            log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Backup " + backup_info.ip_address + " não respondeu. Pode estar offline.");
            // Aqui você poderia adicionar uma lógica para remover o backup da lista.
        }
    } 
    
    close(replication_socket); // Fecha o socket temporário.
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

bool Server::isDuplicateRequest(const string &clientIP, uint32_t seq) {
    lock_guard<mutex> lock(participantsMutex);
    for (const auto &p : participants) {
        if (p.address == clientIP) {
            return (p.last_req == seq);
        }
    }
    return false;
}

bool Server::checkList(const std::string &ip) {
    for (const auto &backup : server_list) {
        if (backup.ip_address == ip) {
            return true;
        }
    }
    return false;
}

tableClient Server::updateParticipant(const string &clientIP, uint32_t seq, uint32_t num) {
    lock_guard<mutex> lock(participantsMutex);
    for (auto &p : participants) {
        if (p.address == clientIP) {
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

void Server::updateSumTable(uint32_t seq, uint64_t num) {
    lock_guard<mutex> lock(sumMutex);
    sumTotal.num_reqs++;
    sumTotal.sum += num;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
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
