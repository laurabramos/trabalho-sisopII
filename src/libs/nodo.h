#ifndef NODO_H
#define NODO_H

#include <string>
#include <vector>
#include <cstdint>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define BROADCAST_ADDR "255.255.255.255"

// Tipos de mensagens trocadas
enum class Type : uint8_t {
    // Cliente <-> Servidor
    DESC,
    REQ,
    DESC_ACK,
    REQ_ACK,
    NOT_LEADER,

    // Servidor <-> Servidor
    SERVER_DISCOVERY,
    REPLICATION_UPDATE,
    REPLICATION_ACK,
    HEARTBEAT,
    // Mensagens como COORDINATOR, ELECTION, OK_ANSWER não são mais necessárias
    // na arquitetura simplificada, mas podem ser mantidas se desejar.
};

#pragma pack(push, 1)
struct Message {
    Type type;
    uint32_t seq;
    uint32_t num;
    
    // Campos para diferentes propósitos
    uint32_t ip_addr; 
    uint64_t total_sum;
    uint32_t total_reqs;
    uint64_t total_sum_server;
    uint32_t total_reqs_server;
};
#pragma pack(pop)

// Estruturas de dados
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

// Classe base
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
