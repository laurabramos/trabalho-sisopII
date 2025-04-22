#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>
#include <cstdint>

class Client : public Nodo {
public:
    Client();
    ~Client();
    
    void discoverServer(Config config);
    void sendNum(const char *serverIP, Config config);

private:
    int clientSocketBroad;
    int clientSocketUni;
    struct sockaddr_in broadcastAddr;
};

#endif

