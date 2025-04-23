#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>
#include <cstdint>

class Client : public Nodo {
public:
    Client(int Discovery_Port);
    ~Client();
    
    void discoverServer(int Discovery_port, int Request_Port);
    void sendNum(const char *serverIP, int Request_Port);

private:
    int clientSocketBroad;
    int clientSocketUni;
    struct sockaddr_in broadcastAddr;
};

#endif

