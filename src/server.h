#ifndef SERVER_H
#define SERVER_H

#include "nodo.h"
#include <iostream>
#include <netinet/in.h>
#include <cstdint>
#include <thread>
#include <mutex>

using namespace std;
class Server : public Nodo {
    public:
        Server(int Discovery_Port);
        ~Server();
        
        void startListening(int Request_Port);
        void receiveNumbers(int Request_Port);
        void printParticipants(Message number);
        void printInicio();

        
    private:
        int serverSocket;
        struct sockaddr_in serverAddr, clientAddr;
    
        //void handleDiscovery(char* buffer, struct sockaddr_in &clientAddr);

        void handleDiscovery(Message& message, struct sockaddr_in &clientAddr);
        bool checkList(const string& ip);
        void updateParticipant(const string& clientIP, uint32_t num);
        void updateSumTable(uint32_t seq, uint64_t num);
    };
    
    #endif
