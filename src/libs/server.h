#ifndef SERVER_H
#define SERVER_H

#include "nodo.h"
#include <iostream>
#include <netinet/in.h>
#include <cstdint>
#include <thread>
#include <mutex>
#include <chrono>

using namespace std;

enum class ServerRole {
    LEADER,
    BACKUP
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
        bool election_in_progress = false;
        chrono::steady_clock::time_point last_heartbeat_time;

      
        void findLeaderOrCreateGroup();
        void runAsLeader();
        void runAsBackup();

       
        void sendHeartbeats();
        void checkForLeaderFailure();
        void startElection();
        void handleElectionMessage(const struct sockaddr_in& fromAddr);
        void handleCoordinatorMessage(const struct sockaddr_in& fromAddr);
        void listenForServerMessages();
        void listenForClientMessages();

  
        void handleClientDiscovery(const struct sockaddr_in& fromAddr);
        void handleServerDiscovery(const struct sockaddr_in& fromAddr);
        void receiveNumbers();
        bool replicateToBackups(const Message& client_request, const struct sockaddr_in& client_addr, const tableClient& client_state, const tableAgregation& server_state);
        void handleIncomingMessages(int numSocket);
      
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
