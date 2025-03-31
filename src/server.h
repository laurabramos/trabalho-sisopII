#ifndef SERVER_H
#define SERVER_H

#include "nodo.h"
#include <iostream>
#include <netinet/in.h>


class Server : public Nodo {
    public:
        Server();
        ~Server();
        
        void startListening();
        
    private:
        int serverSocket;
        struct sockaddr_in serverAddr, clientAddr;
    
        void handleDiscovery(char* buffer, struct sockaddr_in &clientAddr);
    };
    
    #endif
