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
        std::cout << "Começando server\n";
        Server server;
        server.printInicio();
        server.startListening();
    } else if (mode == "client") {
        std::cout << "Começando client\n";
        Client client;
        client.discoverServer();
    } else {
        std::cerr << "Modo inválido! Use 'server' ou 'client'.\n";
        return 1;
    }

    return 0;
}
