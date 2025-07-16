#ifndef SERVER_H
#define SERVER_H

#include "nodo.h"
#include <iostream>
#include <netinet/in.h>
#include <cstdint>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

// Papel do servidor: simplificado.
enum class ServerRole {
    LEADER,
    BACKUP,
    NEEDS_ELECTION // Papel transitório para forçar o reinício do ciclo
};

class Server : public Nodo {
public:
    Server(int client_port, int req_port, int server_comm_port);
    ~Server();
    void start();

private:
    // Portas e identificação
    int client_discovery_port;
    int client_request_port;
    int server_communication_port;
    std::string my_ip;
    std::string leader_ip;

    // Sockets
    int server_socket;
    int client_socket;

    // Estado e controlo
    ServerRole role;
    std::chrono::steady_clock::time_point last_heartbeat_time;
    
    // Funções principais do ciclo de vida
    void findAndElect();
    void runAsLeader();
    void runAsBackup();

    // Threads e Handlers
    void sendHeartbeats();
    void listenForBackupMessages();
    void checkForLeaderFailure();
    void listenForClientDiscovery();
    void listenForServerMessages();
    void receiveNumbers();

    // Handlers de mensagens específicas
    void handleServerDiscovery(const struct sockaddr_in& fromAddr);
    
    // Funções de negócio e replicação
    void handleStateTransferRequest(const struct sockaddr_in& fromAddr);    
    void handleClientDiscovery(int discovery_socket, const struct sockaddr_in& fromAddr);
    bool isDuplicateRequest(const std::string& clientIP, uint32_t seq);
    void applyReplicationState(const Message& replication_msg);
    tableClient updateParticipant(const std::string& clientIP, uint32_t seq, uint32_t num);
    void updateSumTable(uint32_t seq, uint64_t num);
    void printInicio();
    void printParticipants(const std::string& clientIP, const std::string& role_prefix);
    void printRepet(const std::string& clientIP, uint32_t duplicate_seq);
    bool checkList(const std::string& ip);
    bool replicateToBackups(const Message& client_request, const struct sockaddr_in& client_addr, const tableClient& client_state, const tableAgregation& server_state);
    void setParticipantState(const std::string& clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs);
};

#endif // SERVER_H
