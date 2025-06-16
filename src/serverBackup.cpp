#include "libs/serverBackup.h"

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include "nodo.h"

#define PORT 5002       // Porta que o backup vai escutar
#define BUFFER_SIZE 1024
using namespace std;


    
void startwatching(){
    createSocket(sockback, PORT);
    // Limpar e configurar endereço do servidor (backup)
    memset(&servaddr, 0, sizeof(servaddr));
}

void messagesave() {
    int sockback;
    char buffer[BUFFER_SIZE];
    socklen_t len;
    int soma_local = 0;

    while (true) {
        struct sockaddr_in cliaddr;
        socklen_t len = sizeof(cliaddr);
        recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&cliaddr, &len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        string mensagem(buffer);
        // Espera mensagem apenas com números
        try {
            int valor = stoi(mensagem);
            soma_local = valor;

            // Enviar ACK: "ACK"
            string ack_confirma = "ACK";
            sendto(sockback, ack_confirma.c_str(), ack_confirma.size(), 0, (struct sockaddr *)&cliaddr, len);
        } catch (const std::exception& e) {
            // Ignora mensagens inválidas
            continue;
        }
    }

    close(sockback);
    return 0;
}


