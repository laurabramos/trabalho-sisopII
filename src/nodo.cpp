#include "libs/nodo.h"
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstdint>
#include <ifaddrs.h> // Para a nova função getIP

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

// ======================================================================
// CORREÇÃO CRÍTICA 2: Substituída a função getIP() inteira.
// Esta nova versão itera sobre as interfaces de rede para encontrar o
// endereço IPv4 correto, funcionando em contêineres e máquinas reais.
// ======================================================================
string Nodo::getIP() {
    struct ifaddrs *ifaddr, *ifa;
    int family;
    char host[NI_MAXHOST];
    string ipAddress = "127.0.0.1"; // Retorna localhost por padrão se nada for encontrado

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return ipAddress;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        if (family == AF_INET) { // Apenas endereços IPv4
            // Ignora a interface de loopback "lo"
            if (strcmp(ifa->ifa_name, "lo") != 0) {
                if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                    ipAddress = host;
                    break; // Pega o primeiro IP válido que não seja de loopback
                }
            }
        }
    }

    freeifaddrs(ifaddr);
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
        memset(&addr, 0, sizeof(addr)); 
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        // CORREÇÃO CRÍTICA 1: Converter INADDR_ANY para network byte order.
        addr.sin_addr.s_addr = htonl(INADDR_ANY); 

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
        cerr << "Failed to set SO_BROADCAST: " << strerror(errno) << endl;
    }
}

void Nodo::setSocketTimeout(int sockfd, int timeoutSec)
{
    struct timeval timeout;
    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
    {
        cerr << "Failed to set SO_RCVTIMEO: " << strerror(errno) << endl;
    }
}
