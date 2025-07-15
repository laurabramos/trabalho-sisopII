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

// Papel atual do servidor no cluster
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
    void checkForLeaderFailure();
    void listenForServerMessages();
    void receiveNumbers();

    // Handlers de mensagens específicas
    void handleServerDiscovery(const struct sockaddr_in& fromAddr);
    void handleElectionMessage(const struct sockaddr_in& fromAddr);
    
    // Funções de negócio e replicação (a implementar/rever depois)
    void handleClientDiscovery(const struct sockaddr_in& fromAddr);
    bool isDuplicateRequest(const std::string& clientIP, uint32_t seq);
    tableClient updateParticipant(const std::string& clientIP, uint32_t seq, uint32_t num);
    void updateSumTable(uint32_t seq, uint64_t num);
    void printParticipants(const std::string& clientIP);
    void printRepet(const std::string& clientIP, uint32_t duplicate_seq);
    bool checkList(const std::string& ip);
};

#endif // SERVER_H
