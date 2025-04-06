#ifndef NODO_H
#define NODO_H

#include <string>
#include <vector>
#include <cstdint>

#define DISCOVERY_PORT 5000
#define RESQUEST_PORT 5001
#define BUFFER_SIZE 1024

enum class Type { //tipo de mensagens trocadas servidor-cliente
    DESC,
    REQ,
    DESC_ACK,
    REQ_ACK
};

// Estrutura contendo o enum e um identificador opcional
struct Message {
    Type type; //tipo da mensagem que será enviado
    uint32_t num;
    uint32_t seq;
};

struct tableClient {
    std::string address;
    uint32_t last_req;
    uint32_t last_sum;
};

struct tableAgregation {
    uint32_t num_reqs;
    uint64_t sum;
};

class Nodo {
    
    public:
        Nodo();
        virtual ~Nodo();

        virtual std::string getHostname();
        virtual std::string getIP();

    protected:
        std::vector<tableClient> participants; // Lista de clientes (no servidor)
        tableAgregation sumTotal = {0, 0}; // Total de requisições e soma total (no servidor)
};

#endif // NODO_H
