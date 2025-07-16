
#include "libs/nodo.h"
#include <netdb.h>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstdint>
#include <sys/ioctl.h>
#include <net/if.h>

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

std::string Nodo::getIP() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        perror("socket");
        return "Erro ao criar socket";
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFADDR, &ifr) == -1) {
        perror("ioctl");
        close(fd);
        return "Erro ao obter IP";
    }

    close(fd);

    struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    return std::string(inet_ntoa(ipaddr->sin_addr));
}


int Nodo::createSocket(int port)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        cerr << "ERROR opening socket." << endl;
        return -1;
    }

     setSocketTimeout(sockfd, 1); 

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
        throw runtime_error("Failed to set socket options");
    }
}

void Nodo::setSocketTimeout(int sockfd, int timeoutSec)
{
    struct timeval timeout;
    timeout.tv_sec = timeoutSec;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
}

