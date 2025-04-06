#include "nodo.h"
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstdint>

Nodo::Nodo() {}

Nodo::~Nodo() {}

std::string Nodo::getHostname() {
    char hostname[1024];
    hostname[1023] = '\0';

    if (gethostname(hostname, sizeof(hostname) - 1) == -1) {
        perror("gethostname");
        return "Erro ao obter hostname";
    }
    return std::string(hostname);
}

std::string Nodo::getIP() {
    std::string hostname = getHostname();

    struct addrinfo hints{}, *info, *p;
    hints.ai_family = AF_INET; // Apenas IPv4
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &info) != 0) {
        perror("getaddrinfo");
        return "Erro ao obter IP";
    }

    std::string ipAddress = "Desconhecido";
    for (p = info; p != nullptr; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
        ipAddress = inet_ntoa(addr->sin_addr);
        break; // Pega o primeiro IP disponível
    }

    freeaddrinfo(info);
    return ipAddress;
}//@todo
