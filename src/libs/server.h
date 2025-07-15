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

using namespace std;

// CORREÇÃO: Reintroduzida a definição de ServerRole.
enum class ServerRole {
    LEADER,
    BACKUP
};

enum class ServerState {
    NORMAL,
    ELECTION_RUNNING,
    WAITING_FOR_COORDINATOR
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

    ServerRole role; // Agora o tipo 'ServerRole' é conhecido.
    string my_ip;
    string leader_ip;

    atomic<ServerState> current_state; 
    std::atomic<bool> election_requested;
    chrono::steady_clock::time_point last_heartbeat_time;
    chrono::steady_clock::time_point election_start_time;
    
    void findLeaderOrCreateGroup();
    void runAsLeader();
    void runAsBackup();
    
    void sendHeartbeats();
    void checkForLeaderFailure();
    void startElection();
    void waitForNewLeader();
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
