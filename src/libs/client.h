#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>
#include <cstdint>
#include <string> 
#include <queue> 

class Client : public Nodo {
public:
    Client(int Discovery_Port);
    ~Client();
    
    std::string discoverServer(int Discovery_port, int Request_Port); 
    bool sendOneRequest(uint32_t num, std::string& currentServerIP, int requestPort);
    bool sendNum(const std::string& serverIP, int Request_Port);

private:
    int clientSocketUni;
    struct sockaddr_in broadcastAddr;

    uint32_t current_seq = 1; 
    std::queue<uint32_t> unacked_nums; 
};

#endif
