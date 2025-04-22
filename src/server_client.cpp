#include "server.h"
#include "client.h"
#include <cstdint>

using namespace std;

int main(int argc, char* argv[]) {
    Config config;
    cin >> config.Discovery_Port;
    
    if (argc != 2) {
        cerr << "Uso: " << argv[0] << " <server|client>\n";
        return 1;
    }

    string mode = argv[1];

    if (mode == "server") {
        cout << "Começando server\n";
        Server server;
        server.startListening(config);
    } else if (mode == "client") {
        cout << "Começando client\n";
        Client client;
        client.discoverServer(config);
    } else {
        cerr << "Modo inválido! Use 'server' ou 'client'.\n";
        return 1;
    }

    return 0;
}
