#ifndef NODO_H
#define NODO_H

#include <string>
#include <vector>
#include <cstdint>

#define BUFFER_SIZE 1024
#define BROADCAST_ADDR "255.255.255.255"

//tipo de mensagens trocadas servidor-cliente
enum class Type : uint8_t {
    DESC,
    REQ,
    DESC_ACK,
    REQ_ACK
};

// Estrutura contendo o enum e um identificador opcional
#pragma pack(push, 1)
struct Message {
    Type type;
    uint32_t num;
    uint32_t seq;
};
#pragma pack(pop)


struct tableClient {//estrutura da tabela de clientes
    std::string address;
    uint32_t last_req;
    uint32_t last_sum;
    uint32_t last_value;
};

struct tableAgregation {//estrutura da tabela de soma
    uint32_t num_reqs;
    uint64_t sum;
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
        std::vector<tableClient> participants; // Lista de clientes (no servidor)
        tableAgregation sumTotal = {0, 0}; // Total de requisições e soma total (no servidor)
};

#endif // NODO_H
