#include "server.h"
#include "client.h"
#include <cstdint>


int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " <server|client>\n";
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "server") {
        Server server;
        server.startListening();
    } else if (mode == "client") {
        Client client;
        client.discoverServer();
    } else {
        std::cerr << "Modo inválido! Use 'server' ou 'client'.\n";
        return 1;
    }

    return 0;
}
