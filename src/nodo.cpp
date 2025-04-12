#include "nodo.h"
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstdint>

using namespace std;

Nodo::Nodo() {}

Nodo::~Nodo() {}

string Nodo::getHostname() {
    char hostname[1024];
    hostname[1023] = '\0';

    if (gethostname(hostname, sizeof(hostname) - 1) == -1) {
        perror("gethostname");
        return "Erro ao obter hostname";
    }
    return string(hostname);
}

string Nodo::getIP() {
    string hostname = getHostname();

    struct addrinfo hints{}, *info, *p;
    hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &info) != 0) {
        perror("getaddrinfo");
        return "Erro ao obter IP";
    }

    string ipAddress = "Desconhecido";
    for (p = info; p != nullptr; p = p->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in *)p->ai_addr;
        ipAddress = inet_ntoa(addr->sin_addr);
        break; // Pega o primeiro IP disponível
    }

    freeaddrinfo(info);
    return ipAddress;
}

int Nodo::createSocket(int port)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        cerr << "ERROR opening socket." << endl;
        return -1;
    }

    // setSocketTimeout(sockfd, 1); // Timeout de 1 segundo

    if (port != 0)
    {
        int reuse = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
        {
             cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << endl;
            close(sockfd);
            return -1;
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        bzero(&(addr.sin_zero), 8);

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0)
        {
            cerr << "ERROR on binding socket. (" << port << "): " << strerror(errno) << endl;
            close(sockfd);
            return -1;
        }
    }

    return sockfd;
}

void Nodo::setSocketBroadcastOptions(int sockfd)
{
    const int optval{1};
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval)) < 0)
    {
        throw std::runtime_error("Failed to set socket options");
    }
}

void Nodo::setSocketTimeout(int sockfd, int timeoutSec)
{
    struct timeval timeout;
    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
}

