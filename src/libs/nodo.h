#ifndef NODO_H
#define NODO_H

#include <string>
#include <vector>
#include <cstdint>
#include <arpa/inet.h> // Para inet_addr

#define BUFFER_SIZE 1024
#define BROADCAST_ADDR "255.255.255.255"

//tipo de mensagens trocadas
enum class Type : uint8_t {
    // Mensagens Cliente <-> Servidor
    DESC,
    REQ,
    DESC_ACK,
    REQ_ACK,

    // Mensagens Servidor <-> Servidor (Descoberta e Replicação)
    SERVER_DISCOVERY,
    SERVER_DISCOVERY_ACK,
    REPLICATION_UPDATE,
    REPLICATION_ACK,
    LEADER_ANNOUNCEMENT,
    BACKUP_ACK,

    // Mensagens de Eleição (Algoritmo Bully)
    HEARTBEAT,          // Líder prova que está vivo
    ELECTION,           // Um servidor inicia uma eleição
    OK_ANSWER,          // Resposta a uma mensagem de eleição
    COORDINATOR,         // Anúncio do novo líder

    I_AM_ALIVE,
    ARE_YOU_ALIVE,

    REQUEST_STATE_VOTE,  
    STATE_VOTE_RESPONSE, 
    SEND_FULL_STATE,     
    STATE_SYNC_UPDATE  
};

#pragma pack(push, 1)
struct Message {
    Type type;
    uint32_t num;
    uint32_t seq;
    uint32_t ip_addr; 

    uint64_t total_sum;     
    uint32_t total_reqs;     

    
    uint64_t total_sum_server; 
    uint32_t total_reqs_server; 
};
#pragma pack(pop)


struct tableClient {
    std::string address;
    uint32_t last_req;
    uint64_t last_sum;
    uint32_t last_value;
};

struct tableAgregation {
    uint32_t num_reqs;
    uint64_t sum;
};

struct ServerInfo {
    std::string ip_address;
};


class Nodo {
    
    public:
        Nodo();
        virtual ~Nodo();

        virtual std::string getHostname();
        virtual std::string getIP();
        int createSocket(int port);
        void setSocketBroadcastOptions(int sockfd);
        void setSocketTimeout(int sockfd, int timeoutSec);

    protected:
        std::vector<tableClient> participants;
        std::vector<ServerInfo> server_list;
        tableAgregation sumTotal = {0, 0};
};

#endif // NODO_H
