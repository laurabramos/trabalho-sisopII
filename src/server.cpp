#include <iostream>
#include "libs/serversave.h"
#include "libs/serverbackup.h"
#include <cstring>

using namespace std;
//aqui vai ser onde vai chamar o server backup e o server primario o serversave;
int main(int argc, char *argv[])
{
    int Discovery_Port;
    cerr << argv[1] << endl;
    Discovery_Port = atoi(argv[1]);
   
    int Request_Port = Discovery_Port + 1;

    Server server(Discovery_Port);
    server.printInicio();
    server.startListening(Request_Port);
}