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

mutex participantsMutex;
mutex sumMutex;
mutex serverListMutex;

void log_with_timestamp(const string &message) {
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}

// Função de utilidade para converter IP. Colocada fora da classe.
uint32_t ipToInt(const std::string &ipStr) {
    struct in_addr ip_addr;
    inet_aton(ipStr.c_str(), &ip_addr);
    return ntohl(ip_addr.s_addr);
}

Server::Server(int client_port, int req_port, int server_comm_port) {
    this->client_discovery_port = client_port;
    this->client_request_port = req_port;
    this->server_communication_port = server_comm_port;
    this->my_ip = getIP();
    this->server_socket = -1;
    this->client_socket = -1;
    this->role = ServerRole::NEEDS_ELECTION;
}

Server::~Server() {
    if (server_socket != -1)
        close(server_socket);
    if (client_socket != -1)
        close(client_socket);
}

// --- ARQUITETURA PRINCIPAL ---
void Server::start() {
    thread client_comm_thread(&Server::receiveNumbers, this);
    thread client_discovery_thread(&Server::listenForClientDiscovery, this);
    while (true) {
        findAndElect();
        while (role == ServerRole::LEADER || role == ServerRole::BACKUP) {
            if (role == ServerRole::LEADER) {
                runAsLeader();
            } else {
                runAsBackup();
            }
        }
        log_with_timestamp("[" + my_ip + "] Evento de re-eleição despoletado. Reiniciando processo...");
        if (server_socket != -1) {
            close(server_socket);
            server_socket = -1;
        }
        this_thread::sleep_for(chrono::seconds(2));
    }
    client_comm_thread.join();
    client_discovery_thread.join();
}

void Server::findAndElect() {
    log_with_timestamp("[" + my_ip + "] --- INICIANDO FASE DE DESCOBERTA E ELEIÇÃO ---");
    leader_ip = "";
    server_list.clear();
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
    for (int i = 0; i < 2; ++i) {
        sendto(server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::milliseconds(250));
    }
    setSocketTimeout(server_socket, 3);
    auto listen_start_time = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - listen_start_time).count() < 3) {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;
            lock_guard<mutex> lock(serverListMutex);
            if (!checkList(from_ip)) {
                server_list.push_back({from_ip});
                log_with_timestamp("[" + my_ip + "] Servidor " + from_ip + " detectado.");
            }
        }
    }
    server_list.push_back({my_ip});
    string new_leader_ip = my_ip;
    for (const auto &server : server_list) {
        if (ipToInt(server.ip_address) > ipToInt(new_leader_ip)) {
            new_leader_ip = server.ip_address;
        }
    }
    this->leader_ip = new_leader_ip;
    if (this->leader_ip != this->my_ip) {
        log_with_timestamp("[" + my_ip + "] Nó não eleito. Sincronizando estado com o líder " + this->leader_ip + "...");
        participants.clear();
        sumTotal = {0, 0};
        Message request_msg = {Type::STATE_TRANSFER_REQUEST, 0, 0};
        struct sockaddr_in leader_addr = {};
        leader_addr.sin_family = AF_INET;
        leader_addr.sin_port = htons(server_communication_port);
        inet_pton(AF_INET, this->leader_ip.c_str(), &leader_addr.sin_addr);
        sendto(server_socket, &request_msg, sizeof(request_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));
        log_with_timestamp("[" + my_ip + "] Aguardando transferência de estado...");
        setSocketTimeout(server_socket, 5);
        auto transfer_start_time = chrono::steady_clock::now();
        while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - transfer_start_time).count() < 5) {
            Message msg;
            if (recvfrom(server_socket, &msg, sizeof(msg), 0, NULL, NULL) > 0) {
                if (msg.type == Type::STATE_TRANSFER_PAYLOAD) {
                    if (msg.ip_addr == 0) { /* aplica estado global */
                    } else { /* aplica estado de participante */
                    }
                }
            }
        }
        log_with_timestamp("[" + my_ip + "] Sincronização concluída ou timeout.");
    }
    if (this->leader_ip == this->my_ip) {
        this->role = ServerRole::LEADER;
    } else {
        this->role = ServerRole::BACKUP;
    }
    log_with_timestamp("[" + my_ip + "] Eleição concluída. Função final: " + (this->role == ServerRole::LEADER ? "LÍDER" : "BACKUP") + ". Líder definido como: " + this->leader_ip);
}

// --- LÓGICA DE OPERAÇÃO ---
void Server::runAsLeader() {
    log_with_timestamp("--- MODO LÍDER ATIVADO ---");
    this->leader_ip = my_ip;
    thread heartbeat_thread(&Server::sendHeartbeats, this);
    thread server_listener_thread(&Server::listenForServerMessages, this);
    while (role == ServerRole::LEADER) {
        this_thread::sleep_for(chrono::seconds(1));
    }
    log_with_timestamp("Deixando o papel de líder...");
    heartbeat_thread.join();
    server_listener_thread.join();
}

void Server::runAsBackup() {
    log_with_timestamp("--- MODO BACKUP ATIVADO. Líder: " + this->leader_ip + " ---");
    log_with_timestamp("[" + my_ip + "] [BACKUP] Solicitando estado completo do líder " + this->leader_ip);
    Message request_msg = {Type::STATE_TRANSFER_REQUEST, 0, 0};
    struct sockaddr_in leader_addr = {};
    leader_addr.sin_family = AF_INET;
    leader_addr.sin_port = htons(server_communication_port);
    inet_pton(AF_INET, this->leader_ip.c_str(), &leader_addr.sin_addr);
    sendto(server_socket, &request_msg, sizeof(request_msg), 0, (struct sockaddr *)&leader_addr, sizeof(leader_addr));
    last_heartbeat_time = chrono::steady_clock::now();
    thread failure_detection_thread(&Server::checkForLeaderFailure, this);
    thread backup_listener_thread(&Server::listenForBackupMessages, this);
    while (role == ServerRole::BACKUP) {
        this_thread::sleep_for(chrono::seconds(1));
    }
    failure_detection_thread.join();
    backup_listener_thread.join();
}

// --- THREADS E HANDLERS ---
void Server::handleStateTransferRequest(const struct sockaddr_in &fromAddr) {
    string requester_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] [LEADER] Recebido pedido de transferência de estado de " + requester_ip);
    lock_guard<mutex> lock_p(participantsMutex);
    lock_guard<mutex> lock_s(sumMutex);
    Message global_state_msg = {};
    global_state_msg.type = Type::STATE_TRANSFER_PAYLOAD;
    global_state_msg.total_sum_server = sumTotal.sum;
    global_state_msg.total_reqs_server = sumTotal.num_reqs;
    sendto(server_socket, &global_state_msg, sizeof(global_state_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
    for (const auto &p : participants) {
        Message participant_msg = {};
        participant_msg.type = Type::STATE_TRANSFER_PAYLOAD;
        inet_pton(AF_INET, p.address.c_str(), &participant_msg.ip_addr);
        participant_msg.seq = p.last_req;
        participant_msg.num = p.last_value;
        participant_msg.total_sum = p.last_sum;
        sendto(server_socket, &participant_msg, sizeof(participant_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    log_with_timestamp("[" + my_ip + "] [LEADER] Transferência de estado para " + requester_ip + " concluída.");
}

void Server::checkForLeaderFailure() {
    last_heartbeat_time = chrono::steady_clock::now();
    const int HEARTBEAT_TIMEOUT_SEC = 6;
    while (role == ServerRole::BACKUP) {
        this_thread::sleep_for(chrono::seconds(1));
        if (leader_ip.empty() || leader_ip == my_ip) {
            continue;
        }
        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count() > HEARTBEAT_TIMEOUT_SEC) {
            log_with_timestamp("[" + my_ip + "] Líder (" + leader_ip + ") inativo. Reiniciando para forçar nova eleição.");
            role = ServerRole::NEEDS_ELECTION;
            return;
        }
    }
}

void Server::sendHeartbeats() {
    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
    Message hb_msg = {Type::HEARTBEAT};
    while (role == ServerRole::LEADER) {
        sendto(server_socket, &hb_msg, sizeof(hb_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::seconds(2));
    }
}

void Server::listenForServerMessages() {
    while (role == ServerRole::LEADER) {
        setSocketTimeout(server_socket, 1);
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;
            if (msg.type == Type::SERVER_DISCOVERY) {
                handleServerDiscovery(from_addr);
            } else if (msg.type == Type::STATE_TRANSFER_REQUEST) {
                handleStateTransferRequest(from_addr);
            }
        }
    }
}

void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr) {
    string new_server_ip = inet_ntoa(fromAddr.sin_addr);
    lock_guard<mutex> lock(serverListMutex);
    if (!checkList(new_server_ip)) {
        server_list.push_back({new_server_ip});
        log_with_timestamp("[" + my_ip + "] Novo servidor " + new_server_ip + " adicionado à lista.");
    }
    if (this->role == ServerRole::LEADER && ipToInt(new_server_ip) > ipToInt(my_ip)) {
        log_with_timestamp("[" + my_ip + "] Detectado servidor com IP maior (" + new_server_ip + "). Cedendo liderança.");
        this->role = ServerRole::NEEDS_ELECTION;
    }
}

void Server::listenForBackupMessages() {
    while (role == ServerRole::BACKUP) {
        setSocketTimeout(server_socket, 1);
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (msg.type == Type::HEARTBEAT && from_ip == this->leader_ip) {
                last_heartbeat_time = chrono::steady_clock::now();
            } else if (msg.type == Type::REPLICATION_UPDATE && from_ip == this->leader_ip) {
                log_with_timestamp("[" + my_ip + "] [BACKUP] Recebida atualização de estado do líder.");
                applyReplicationState(msg);
                Message ack_msg = {Type::REPLICATION_ACK, msg.seq, 0};
                sendto(server_socket, &ack_msg, sizeof(ack_msg), 0, (struct sockaddr *)&from_addr, from_len);
            } else if (msg.type == Type::STATE_TRANSFER_PAYLOAD && from_ip == this->leader_ip) {
                if (msg.ip_addr == 0) {
                    lock_guard<mutex> lock(sumMutex);
                    sumTotal.sum = msg.total_sum_server;
                    sumTotal.num_reqs = msg.total_reqs_server;
                    log_with_timestamp("[" + my_ip + "] [BACKUP] Estado global sincronizado: " + to_string(sumTotal.sum));
                } else {
                    struct in_addr client_addr = {.s_addr = msg.ip_addr};
                    setParticipantState(inet_ntoa(client_addr), msg.seq, msg.num, msg.total_sum, msg.seq);
                    log_with_timestamp("[" + my_ip + "] [BACKUP] Estado do participante " + string(inet_ntoa(client_addr)) + " sincronizado.");
                }
            }
        }
    }
}

void Server::listenForClientDiscovery() {
    int discovery_socket = createSocket(this->client_discovery_port);
    if (discovery_socket == -1) {
        log_with_timestamp("[" + my_ip + "] ERRO CRÍTICO: Falha ao criar socket de descoberta de cliente.");
        return;
    }
    while (true) {
        Message msg;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int received = recvfrom(discovery_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&clientAddr, &clientLen);
        if (received > 0 && msg.type == Type::DESC) {
            if (this->role == ServerRole::LEADER) {
                log_with_timestamp("[" + my_ip + "] [LEADER] Recebido pedido de descoberta de " + string(inet_ntoa(clientAddr.sin_addr)));
                handleClientDiscovery(discovery_socket, clientAddr);
            }
        }
    }
    close(discovery_socket);
}

void Server::receiveNumbers() {
    int numSocket = createSocket(client_request_port);
    if (numSocket == -1) {
        return;
    }
    while (true) {
        Message number;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int received = recvfrom(numSocket, &number, sizeof(Message), 0, (struct sockaddr *)&clientAddr, &clientLen);
        if (received > 0 && number.type == Type::REQ) {
            if (role == ServerRole::LEADER) {
                string clientIP = inet_ntoa(clientAddr.sin_addr);
                uint32_t last_known_seq = 0;
                {
                    lock_guard<mutex> lock(participantsMutex);
                    for (const auto &p : participants) {
                        if (p.address == clientIP) {
                            last_known_seq = p.last_req;
                            break;
                        }
                    }
                }
                if (number.seq == last_known_seq + 1) {
                    tableClient clientState = updateParticipant(clientIP, number.seq, number.num);
                    updateSumTable(number.seq, number.num);
                    printParticipants(clientIP, "[LEADER] ");
                    tableAgregation server_state_copy;
                    {
                        lock_guard<mutex> lock(sumMutex);
                        server_state_copy = this->sumTotal;
                    }
                    replicateToBackups(number, clientAddr, clientState, server_state_copy);
                    Message confirmation = {};
                    confirmation.type = Type::REQ_ACK;
                    confirmation.seq = number.seq;
                    confirmation.total_sum = clientState.last_sum;
                    confirmation.total_reqs = clientState.last_req;
                    sendto(numSocket, &confirmation, sizeof(confirmation), 0, (struct sockaddr *)&clientAddr, clientLen);
                } else if (number.seq <= last_known_seq) {
                    printRepet(clientIP, number.seq);
                    Message confirmation = {};
                    confirmation.type = Type::REQ_ACK;
                    confirmation.seq = number.seq;
                    {
                        lock_guard<mutex> lock(participantsMutex);
                        for (const auto &p : participants) {
                            if (p.address == clientIP) {
                                confirmation.total_sum = p.last_sum;
                                confirmation.total_reqs = p.last_req;
                                break;
                            }
                        }
                    }
                    sendto(numSocket, &confirmation, sizeof(confirmation), 0, (struct sockaddr *)&clientAddr, clientLen);
                } else {
                    log_with_timestamp("[" + my_ip + "] Requisição futura de " + clientIP + " descartada (esperado: " + to_string(last_known_seq + 1) + ", recebido: " + to_string(number.seq) + ")");
                }
            } else {
                Message redirect_msg = {Type::NOT_LEADER};
                if (!leader_ip.empty()) {
                    inet_pton(AF_INET, leader_ip.c_str(), &redirect_msg.ip_addr);
                }
                sendto(numSocket, &redirect_msg, sizeof(redirect_msg), 0, (struct sockaddr *)&clientAddr, clientLen);
            }
        }
    }
    close(numSocket);
}

void Server::printInicio() { log_with_timestamp("num_reqs 0 total_sum 0"); }

void Server::handleClientDiscovery(int discovery_socket, const struct sockaddr_in &fromAddr) {
    string clientIP = inet_ntoa(fromAddr.sin_addr);
    Message response = {Type::DESC_ACK, 0, 0};
    sendto(discovery_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
    lock_guard<mutex> lock(participantsMutex);
    if (!checkList(clientIP)) {
        participants.push_back({clientIP, 0, 0, 0});
    }
}

bool Server::replicateToBackups(const Message &client_request, const struct sockaddr_in &client_addr, const tableClient &client_state, const tableAgregation &server_state) {
    std::vector<ServerInfo> backups_to_notify;
    {
        lock_guard<mutex> lock(serverListMutex);
        if (server_list.empty()) {
            cout << "nao tem ninguém" << endl;
            return true;
        }
        cout << "tem alguem hehehe" << endl;
        backups_to_notify = this->server_list;
    }
    log_with_timestamp("[" + my_ip + "] [LEADER] Iniciando replicação síncrona, um por um, para " + to_string(backups_to_notify.size()) + " backup(s)...");
    int replication_socket = createSocket(0);
    if (replication_socket == -1) {
        log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Falha ao criar socket para replicação.");
        return false;
    }
    setSocketTimeout(replication_socket, 2);
    Message replication_msg = client_request;
    replication_msg.type = Type::REPLICATION_UPDATE;
    replication_msg.ip_addr = client_addr.sin_addr.s_addr;
    replication_msg.total_sum = client_state.last_sum;
    replication_msg.total_reqs = client_state.last_req;
    replication_msg.total_sum_server = server_state.sum;
    replication_msg.total_reqs_server = server_state.num_reqs;
    int successful_acks = 0;
    for (const auto &backup_info : backups_to_notify) {
        if (backup_info.ip_address == my_ip) {
            continue;
        }
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, backup_info.ip_address.c_str(), &dest_addr.sin_addr);
        bool ack_received = false;
        sendto(replication_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(replication_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0) {
            if (response.type == Type::REPLICATION_ACK && response.seq == client_request.seq) {
                ack_received = true;
            }
        }
        if (ack_received) {
            successful_acks++;
            log_with_timestamp("[" + my_ip + "] [LEADER] ACK recebido de " + backup_info.ip_address);
        } else {
            log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Backup " + backup_info.ip_address + " não respondeu. Pode estar offline.");
        }
    }
    close(replication_socket);
    return true;
}

void Server::applyReplicationState(const Message &msg) {
    struct in_addr client_addr_struct = {.s_addr = msg.ip_addr};
    string clientIP = inet_ntoa(client_addr_struct);
    updateParticipant(clientIP, msg.seq, msg.num);
    updateSumTable(msg.seq, msg.num);
    printParticipants(clientIP, "[BACKUP]");
}

void Server::setParticipantState(const std::string &clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs) {
    lock_guard<mutex> lock(participantsMutex);
    for (auto &p : participants) {
        if (p.address == clientIP) {
            p.last_sum = client_sum;
            p.last_req = client_reqs;
            p.last_value = value;
            return;
        }
    }
    participants.push_back({clientIP, client_reqs, client_sum, value});
}

void Server::printParticipants(const std::string &clientIP, const std::string &role_prefix) {
    lock_guard<mutex> lock_participants(participantsMutex);
    lock_guard<mutex> lock_sum(sumMutex);
    for (const auto &p : participants) {
        if (p.address == clientIP) {
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

void Server::printRepet(const std::string &clientIP, uint32_t duplicate_seq) {
    lock_guard<mutex> lock_participants(participantsMutex);
    lock_guard<mutex> lock_sum(sumMutex);
    for (const auto &p : participants) {
        if (p.address == clientIP) {
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
            return (seq <= p.last_req);
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
