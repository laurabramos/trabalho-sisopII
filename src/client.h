// client.h - VERSÃO CORRIGIDA
#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>
#include <cstdint>
#include <string> 

class Client : public Nodo {
public:
    Client(int Discovery_Port);
    ~Client();
    
    std::string discoverServer(int Discovery_port, int Request_Port); 
    bool sendNum(const std::string& serverIP, int Request_Port);

private:
    //int clientSocketBroad;
    int clientSocketUni;
    struct sockaddr_in broadcastAddr;

    uint32_t current_seq = 1; 
};

#endif