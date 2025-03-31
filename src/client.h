#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>

class Client : public Nodo {
public:
    Client();
    ~Client();
    
    void discoverServer();
    
private:
    int clientSocket;
    struct sockaddr_in broadcastAddr;
};

#endif

