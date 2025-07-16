#ifndef CLIENT_H
#define CLIENT_H

#include "nodo.h"
#include <netinet/in.h>
#include <cstdint>
#include <string> 
#include <queue> 
#include <thread>
#include <mutex>
#include <atomic>

class Client : public Nodo {
public:
    Client(int discovery_port, int server_comm_port);
    ~Client();
    
    // Procura o líder inicial na rede e bloqueia até encontrar.
    void discoverInitialServer(); 
    
    // Envia números para o líder atual
    bool sendNum(int request_port);

    // Gerencia a thread do listener
    void startListener();
    void stopListener();
    bool isRunning() const;

private:
    // Função executada pela thread para ouvir anúncios de COODINATOR
    void listenForCoordinator();

    int discovery_port;
    int server_comm_port;

    std::string serverIP;          // IP do líder atual (recurso compartilhado)
    std::mutex ip_mutex;           // Mutex para proteger o acesso a serverIP
    std::thread coordinator_listener_thread; // A thread do listener
    std::atomic<bool> running{true};         // Flag para controlar o ciclo de vida da thread e dos loops

    uint32_t current_seq = 1; 
    std::queue<uint32_t> unacked_nums; 
};

#endif