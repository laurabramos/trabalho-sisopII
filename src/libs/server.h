#ifndef SERVER_H
#define SERVER_H

#include "nodo.h"
#include <iostream>
#include <netinet/in.h>
#include <cstdint>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic> // ALTERAÇÃO: Adicionado para std::atomic

using namespace std;

// ALTERAÇÃO: Estado do servidor para uma máquina de estados mais robusta.
// Isso previne condições de corrida e loops de eleição.
enum class ServerState {
    NORMAL,                 // Operando normalmente como LÍDER ou BACKUP.
    ELECTION_RUNNING,       // Ativamente conduzindo uma eleição que ele mesmo iniciou.
    WAITING_FOR_COORDINATOR // Perdeu uma eleição e está aguardando o novo líder se anunciar.
};

class Server : public Nodo {
public:
    Server(int client_port, int req_port, int server_comm_port);
    ~Server();
    
    void start();

private:
    int client_discovery_port;
    int client_request_port;
    int server_communication_port;
    
    int server_socket;
    int client_socket;

    ServerRole role;
    string my_ip;
    string leader_ip;

    // ALTERAÇÃO: Substituído o 'bool election_in_progress' por um estado atômico mais seguro.
    atomic<ServerState> current_state; 
    chrono::steady_clock::time_point last_heartbeat_time;
    
    void findLeaderOrCreateGroup();
    void runAsLeader();
    void runAsBackup();
    
    void sendHeartbeats();
    void checkForLeaderFailure();
    void startElection();
    void waitForNewLeader(); // ALTERAÇÃO: Nova função para o estado de espera pós-eleição.
    void handleElectionMessage(const struct sockaddr_in& fromAddr);
    void handleCoordinatorMessage(const struct sockaddr_in& fromAddr);
    void listenForServerMessages();
    void listenForClientMessages();
    
    void handleClientDiscovery(const struct sockaddr_in& fromAddr);
    void handleServerDiscovery(const struct sockaddr_in& fromAddr);
    void receiveNumbers();
    bool replicateToBackups(const Message& client_request, const struct sockaddr_in& client_addr, const tableClient& client_state, const tableAgregation& server_state);
    
    void printInicio();
    bool isDuplicateRequest(const string &clientIP, uint32_t seq);
    tableClient updateParticipant(const std::string &clientIP, uint32_t seq, uint32_t num);
    void updateSumTable(uint32_t seq, uint64_t num);
    void printParticipants(const std::string &clientIP);
    void printRepet(const std::string &clientIP, uint32_t seq);
    bool checkList(const string& ip);

    void setParticipantState(const std::string& clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs);
};
    
#endif // SERVER_H
