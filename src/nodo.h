#ifndef NODO_H
#define NODO_H

#include <string>
#include <vector>

#define DISCOVERY_PORT 5000
#define BUFFER_SIZE 1024

class Nodo {
    public:
        Nodo();
        virtual ~Nodo();

        virtual std::string getHostname();
        virtual std::string getIP();

    protected:
        std::vector<std::string> participants; // Lista de participantes (no servidor)
};

#endif // NODO_H
