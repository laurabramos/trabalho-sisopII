#include "libs/server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <random> // ALTERAÇÃO: Melhor gerador de números aleatórios
#include <string>
#include <algorithm>

using namespace std;

mutex participantsMutex;
mutex sumMutex;
mutex serverListMutex;

void log_with_timestamp(const string &message)
{
    time_t now = time(0);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    cout << timestamp << " " << message << endl;
}

Server::Server(int client_port, int req_port, int server_comm_port)
{
    this->client_discovery_port = client_port;
    this->client_request_port = req_port;
    this->server_communication_port = server_comm_port;
    this->my_ip = getIP();
    this->server_socket = -1;
    this->client_socket = -1;
    this->current_state = ServerState::NORMAL;
    this->role = ServerRole::NEEDS_ELECTION;
    this->election_requested = false;
}

uint32_t ipToInt(const std::string &ipStr) {
    struct in_addr ip_addr;
    inet_aton(ipStr.c_str(), &ip_addr);
    return ntohl(ip_addr.s_addr);
}

Server::~Server()
{
    if (server_socket != -1)
        close(server_socket);
    if (client_socket != -1)
        close(client_socket);
}

void Server::start()
{

    // Inicia a thread de comunicação com o cliente UMA VEZ.
    // Ela vai rodar para sempre, adaptando seu comportamento ao papel do servidor.
    thread client_comm_thread(&Server::receiveNumbers, this);

    while (true)
    {

        findAndElect();

        while (this->role == ServerRole::LEADER || this->role == ServerRole::BACKUP)
        {
            if (this->role == ServerRole::LEADER)
            {
                runAsLeader();
            }
            else if (this->role == ServerRole::BACKUP)
            {
                runAsBackup();
            }
        }

        log_with_timestamp("[" + my_ip + "] Ocorreu um evento. Reiniciando descoberta de grupo.");
        close(this->server_socket);
        this->server_socket = -1;
        this_thread::sleep_for(chrono::seconds(2));
    }

    // Se o loop principal terminar um dia, junte-se à thread.
    client_comm_thread.join();
}

void Server::findAndElect()
{
    log_with_timestamp("[" + my_ip + "] --- INICIANDO FASE DE DESCOBERTA E ELEIÇÃO ---");

    // Reinicia o estado para um começo limpo
    this->leader_ip = "";
    this->server_list.clear();
    this->role = ServerRole::BACKUP; // Assume-se temporariamente como backup
    this->current_state = ServerState::NORMAL;

    // Cria o socket de comunicação entre servidores
    this->server_socket = createSocket(this->server_communication_port);
    if (this->server_socket == -1)
    {
        log_with_timestamp("Falha crítica ao criar socket. Encerrando.");
        exit(1);
    }
    setSocketBroadcastOptions(this->server_socket);

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);

    // FASE 1: ANUNCIAR PRESENÇA (BROADCAST)
    log_with_timestamp("[" + my_ip + "] Fase 1: Anunciando presença na rede...");
    Message discovery_msg = {Type::SERVER_DISCOVERY};
    for (int i = 0; i < 3; ++i)
    {
        sendto(this->server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::milliseconds(300));
    }

    // FASE 2: ESCUTAR RESPOSTAS E OUTROS SERVIDORES
    log_with_timestamp("[" + my_ip + "] Fase 2: Escutando por outros servidores e líderes...");
    setSocketTimeout(this->server_socket, 4); // Aumenta o tempo de escuta
    auto listen_start_time = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - listen_start_time).count() < 4)
    {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;

            // Se recebermos um HEARTBEAT ou COORD, temos um candidato a líder
            if (response.type == Type::HEARTBEAT || response.type == Type::COORDINATOR)
            {
                if (ipToInt(from_ip) > ipToInt(this->leader_ip))
                {
                    this->leader_ip = from_ip;
                    log_with_timestamp("[" + my_ip + "] Candidato a líder detectado: " + from_ip);
                }
            }

            // Adiciona qualquer servidor que responder à lista de conhecidos
            lock_guard<mutex> lock(serverListMutex);
            if (!checkList(from_ip))
            {
                server_list.push_back({from_ip});
                log_with_timestamp("[" + my_ip + "] Servidor " + from_ip + " detectado.");
            }
        }
    }

    // FASE 3: DECISÃO
    if (!this->leader_ip.empty() && ipToInt(this->my_ip) < ipToInt(this->leader_ip))
    {
        // Encontramos um líder e ele é mais forte. Nos tornamos backup.
        this->role = ServerRole::BACKUP;
        log_with_timestamp("[" + my_ip + "] Líder estável encontrado em " + this->leader_ip + ". Tornando-me BACKUP.");
        return; // Fim da fase, pronto para operar.
    }

    // Se não há líder, ou se o líder encontrado é mais fraco, uma eleição é necessária.
    log_with_timestamp("[" + my_ip + "] Nenhum líder válido encontrado. Iniciando eleição geral.");
    startElection();

    // A eleição pode demorar. Entramos num loop de espera pelo resultado.
    log_with_timestamp("[" + my_ip + "] Aguardando resultado da eleição...");
    auto election_wait_start = chrono::steady_clock::now();
    const int ELECTION_WAIT_TIMEOUT = 15; // Timeout generoso para a eleição resolver-se

    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - election_wait_start).count() < ELECTION_WAIT_TIMEOUT)
    {
        if (!this->leader_ip.empty() && this->leader_ip != this->my_ip)
        {
            // Outro servidor se tornou líder, e nós o aceitamos.
            this->role = ServerRole::BACKUP;
            log_with_timestamp("[" + my_ip + "] Eleição concluída. Novo líder é " + this->leader_ip);
            return;
        }
        if (this->role == ServerRole::LEADER)
        {
            // Nós nos tornamos o líder.
            log_with_timestamp("[" + my_ip + "] Eleição concluída. Eu sou o novo líder.");
            return;
        }
        this_thread::sleep_for(chrono::milliseconds(500));
    }

    log_with_timestamp("[" + my_ip + "] Timeout de espera da eleição. Tentando novamente...");
    // Se o timeout expirar, o loop em start() irá recomeçar o processo.
}

void Server::handleServerDiscovery(const struct sockaddr_in &fromAddr)
{
    string new_server_ip = inet_ntoa(fromAddr.sin_addr);
    log_with_timestamp("[" + my_ip + "] Recebi SERVER_DISCOVERY de " + new_server_ip);

    Message response = {Type::SERVER_DISCOVERY_ACK, 0, 0};
    sendto(this->server_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

    lock_guard<mutex> lock(serverListMutex);
    if (!checkList(new_server_ip))
    {
        server_list.push_back({new_server_ip});
        log_with_timestamp("[" + my_ip + "] Servidor " + new_server_ip + " adicionado à lista de backups.");
    }
}

uint32_t ipToInt(const std::string &ipStr)
{
    struct in_addr ip_addr;
    inet_aton(ipStr.c_str(), &ip_addr);
    return ntohl(ip_addr.s_addr);
}

void Server::handleElectionMessage(const struct sockaddr_in &fromAddr)
{
    string challenger_ip = inet_ntoa(fromAddr.sin_addr);
    if (ipToInt(this->my_ip) > ipToInt(challenger_ip))
    {
        log_with_timestamp("[" + my_ip + "] Meu IP é maior. Enviando OK_ANSWER para " + challenger_ip);
        Message answer_msg = {Type::OK_ANSWER, 0, 0};
        sendto(this->server_socket, &answer_msg, sizeof(answer_msg), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));
        // Já que estou a responder, também preciso de me garantir na eleição
        this->role = ServerRole::NEEDS_ELECTION;
    }
}

void Server::handleCoordinatorMessage(const struct sockaddr_in &fromAddr)
{
    string new_leader_ip = inet_ntoa(fromAddr.sin_addr);

    // Ignora se o novo líder anunciado for inferior a um líder que já conhecemos
    if (!this->leader_ip.empty() && ipToInt(new_leader_ip) < ipToInt(this->leader_ip))
    {
        return;
    }

    log_with_timestamp("[" + my_ip + "] Recebi COORDINATOR de " + new_leader_ip + ". Aceitando como novo líder.");
    this->leader_ip = new_leader_ip;
    this->current_state = ServerState::NORMAL;
    this->last_heartbeat_time = chrono::steady_clock::now();
    this->election_requested = false;

    if (this->leader_ip == this->my_ip)
    {
        this->role = ServerRole::LEADER;
    }
    else
    {
        this->role = ServerRole::BACKUP;
    }
}

void Server::findLeaderOrCreateGroup()
{
    log_with_timestamp("[" + my_ip + "] Fase de descoberta iniciada...");
    this->server_socket = createSocket(this->server_communication_port);
    if (this->server_socket == -1)
    {
        exit(1);
    }
    setSocketBroadcastOptions(this->server_socket);

    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);

    // FASE 1: ANUNCIAR PRESENÇA (BROADCAST)
    log_with_timestamp("[" + my_ip + "] Fase 1: Anunciando presença na rede...");
    auto broadcast_start_time = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - broadcast_start_time).count() < 2)
    {
        Message discovery_msg = {Type::SERVER_DISCOVERY, 0, 0};
        sendto(this->server_socket, &discovery_msg, sizeof(discovery_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::milliseconds(500));
    }

    // FASE 2: ESCUTAR RESPOSTAS E OUTROS SERVIDORES
    log_with_timestamp("[" + my_ip + "] Fase 2: Escutando por outros servidores e líderes...");
    setSocketTimeout(this->server_socket, 3);
    auto listen_start_time = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - listen_start_time).count() < 3)
    {
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(this->server_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;

            // ########## CORREÇÃO IMPORTANTE ##########
            // Um HEARTBEAT ou COORDINATOR define o líder.
            if (response.type == Type::HEARTBEAT || response.type == Type::COORDINATOR)
            {
                if (ipToInt(from_ip) > ipToInt(this->leader_ip))
                {
                    this->leader_ip = from_ip;
                    log_with_timestamp("[" + my_ip + "] Líder " + from_ip + " detectado via " + (response.type == Type::HEARTBEAT ? "HEARTBEAT" : "COORDINATOR") + ".");
                }
            }
            // Uma resposta de descoberta ou um anúncio de outro servidor adiciona-o à lista.
            // Isso garante que mesmo que não saibamos quem é o líder, sabemos que não estamos sozinhos.
            else if (response.type == Type::SERVER_DISCOVERY_ACK || response.type == Type::SERVER_DISCOVERY)
            {
                lock_guard<mutex> lock(serverListMutex);
                if (!checkList(from_ip))
                {
                    server_list.push_back({from_ip});
                    log_with_timestamp("[" + my_ip + "] Servidor " + from_ip + " detectado durante a descoberta.");
                }
            }
        }
    }

    // FASE 3: DECISÃO
    if (!this->leader_ip.empty())
    {
        // Compara o meu IP com o do líder encontrado.
        if (ipToInt(this->my_ip) > ipToInt(this->leader_ip))
        {
            // Eu sou mais forte que o líder existente. Devo iniciar uma eleição para tomar o poder.
            log_with_timestamp("[" + my_ip + "] Encontrei um líder mais fraco (" + this->leader_ip + "). Iniciando eleição para assumir.");
            startElection();
            return;
        }
        else
        {
            // O líder encontrado é mais forte ou igual. Aceito meu papel de backup.
            this->role = ServerRole::BACKUP;
            log_with_timestamp("[" + my_ip + "] Líder existente encontrado em " + this->leader_ip + ". Tornando-me BACKUP.");
            return;
        }
    }

    // Se não encontrou um líder, mas encontrou outros servidores, inicia uma eleição adequada.
    // Se não encontrou ninguém, também inicia uma eleição (e vai ganhar).
    log_with_timestamp("[" + my_ip + "] Fase de descoberta encerrada. Nenhum líder ativo encontrado. Iniciando eleição...");
    startElection();
}

void Server::startElection()
{
    this->current_state = ServerState::ELECTION_RUNNING;
    this->election_start_time = chrono::steady_clock::now();

    log_with_timestamp("[" + my_ip + "] Iniciando envio de mensagens de eleição...");

    std::vector<ServerInfo> servers_to_bully;
    {
        lock_guard<mutex> lock(serverListMutex);
        for (const auto &s : server_list)
        {
            if (ipToInt(s.ip_address) > ipToInt(this->my_ip))
            {
                servers_to_bully.push_back(s);
            }
        }
    }

    if (servers_to_bully.empty())
    {
        log_with_timestamp("[" + my_ip + "] Nenhum valentão encontrado. Assumindo liderança.");
        this->role = ServerRole::LEADER;
        this->leader_ip = this->my_ip;
        this->current_state = ServerState::NORMAL;
        return;
    }

    Message election_msg = {Type::ELECTION};
    for (const auto &bully : servers_to_bully)
    {
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, bully.ip_address.c_str(), &dest_addr.sin_addr);
        sendto(this->server_socket, &election_msg, sizeof(election_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    }
    log_with_timestamp("[" + my_ip + "] Mensagens de eleição enviadas. Aguardando respostas...");

    // Espera por respostas (OK_ANSWER)
    this_thread::sleep_for(chrono::seconds(2)); // Tempo para as respostas chegarem

    if (this->current_state == ServerState::ELECTION_RUNNING)
    {
        // Se ninguém respondeu, eu sou o líder.
        log_with_timestamp("[" + my_ip + "] Ninguém maior respondeu. Assumindo liderança.");
        this->role = ServerRole::LEADER;
        this->leader_ip = this->my_ip;
        this->current_state = ServerState::NORMAL;
    }
}

// ALTERAÇÃO: Nova função para aguardar o anúncio do novo líder e evitar loops.
void Server::waitForNewLeader()
{
    log_with_timestamp("[" + my_ip + "] Aguardando anúncio do novo coordenador...");
    this->current_state = ServerState::WAITING_FOR_COORDINATOR;
    const int WAIT_FOR_LEADER_TIMEOUT_SEC = 5;
    setSocketTimeout(this->server_socket, WAIT_FOR_LEADER_TIMEOUT_SEC);

    Message msg;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    if (recvfrom(this->server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
    {
        if (msg.type == Type::COORDINATOR)
        {
            handleCoordinatorMessage(from_addr);
        }
    }
    else
    {
        log_with_timestamp("[" + my_ip + "] Timeout! Nenhum coordenador se anunciou. Iniciando nova eleição.");
        this->current_state = ServerState::NORMAL;
        startElection();
    }
    setSocketTimeout(this->server_socket, 0);
}

// Funções de Papel (Leader, Backup)
void Server::runAsLeader()
{
    log_with_timestamp("--- MODO LÍDER ATIVADO ---");
    // Garante que o estado está correto
    this->leader_ip = this->my_ip;
    this->current_state = ServerState::NORMAL;

    // A fase de anúncio (se necessária) deve ser chamada aqui.
    // Para simplificar, vamos assumir que os heartbeats farão o trabalho.

    thread heartbeat_thread(&Server::sendHeartbeats, this);
    thread server_listener_thread(&Server::listenForServerMessages, this);
    // client_listener_thread não é mais necessária se o redirecionamento funcionar bem

    while (this->role == ServerRole::LEADER)
    {
        this_thread::sleep_for(chrono::seconds(1));
    }

    log_with_timestamp("Deixando o papel de líder...");
    heartbeat_thread.join();
    server_listener_thread.join();
}

void Server::listenForServerMessages()
{
    while (this->role == ServerRole::LEADER)
    {
        setSocketTimeout(this->server_socket, 1);
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;

            switch (msg.type)
            {
            case Type::SERVER_DISCOVERY:
                handleServerDiscovery(from_addr);
                break;
            case Type::COORDINATOR: // Alguém se anunciou como líder
            case Type::HEARTBEAT:   // Ou um líder está a enviar heartbeats
                if (ipToInt(from_ip) > ipToInt(this->my_ip))
                {
                    log_with_timestamp("[" + my_ip + "] Detectei um líder mais forte (" + from_ip + "). Cedendo liderança e reiniciando.");
                    this->role = ServerRole::NEEDS_ELECTION;
                }
                break;
            default:
                break;
            }
        }
    }
}

void Server::listenForClientMessages()
{
    if (this->client_socket == -1)
        this->client_socket = createSocket(this->client_discovery_port);
    if (this->client_socket == -1)
    {
        log_with_timestamp("[" + my_ip + "] LEADER: Falha ao criar socket de descoberta de cliente.");
        return;
    }
    while (this->role == ServerRole::LEADER)
    {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        Message msg;
        setSocketTimeout(this->client_socket, 1);
        if (recvfrom(this->client_socket, &msg, sizeof(Message), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            if (msg.type == Type::DESC)
            {
                handleClientDiscovery(from_addr);
            }
        }
    }
    close(this->client_socket);
    this->client_socket = -1;
}

void Server::sendHeartbeats()
{
    struct sockaddr_in broadcast_addr = {};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(this->server_communication_port);
    inet_pton(AF_INET, BROADCAST_ADDR, &broadcast_addr.sin_addr);
    setSocketBroadcastOptions(this->server_socket);

    Message hb_msg = {Type::HEARTBEAT};

    while (this->role == ServerRole::LEADER)
    {
        sendto(this->server_socket, &hb_msg, sizeof(hb_msg), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        this_thread::sleep_for(chrono::seconds(2));
    }
}

void Server::checkForLeaderFailure()
{
    this->last_heartbeat_time = chrono::steady_clock::now();
    const int HEARTBEAT_TIMEOUT_SEC = 5;

    while (this->role == ServerRole::BACKUP)
    {
        this_thread::sleep_for(chrono::seconds(1));

        if (this->leader_ip.empty() || this->leader_ip == this->my_ip)
        {
            continue;
        }

        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - last_heartbeat_time).count() > HEARTBEAT_TIMEOUT_SEC)
        {
            log_with_timestamp("[" + my_ip + "] Líder (" + leader_ip + ") inativo. Reiniciando para forçar nova eleição.");
            this->role = ServerRole::NEEDS_ELECTION;
            return;
        }
    }
}

void Server::runAsBackup()
{
    log_with_timestamp("--- MODO BACKUP ATIVADO. Líder: " + this->leader_ip + " ---");
    this->current_state = ServerState::NORMAL; // Garante estado limpo

    thread failure_detection_thread(&Server::checkForLeaderFailure, this);

    while (this->role == ServerRole::BACKUP)
    {
        setSocketTimeout(this->server_socket, 1);
        Message msg;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        if (recvfrom(server_socket, &msg, sizeof(msg), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            string from_ip = inet_ntoa(from_addr.sin_addr);
            if (from_ip == my_ip)
                continue;

            // Se a mensagem vem do líder, atualiza o timer.
            if (from_ip == this->leader_ip)
            {
                this->last_heartbeat_time = chrono::steady_clock::now();
            }

            switch (msg.type)
            {
            case Type::HEARTBEAT:
            case Type::COORDINATOR:
                // Se um líder mais forte (ou um novo líder) se anuncia, reinicia o processo.
                if (ipToInt(from_ip) > ipToInt(this->leader_ip))
                {
                    log_with_timestamp("[" + my_ip + "] Detectado novo líder mais forte: " + from_ip + ". Reiniciando.");
                    this->role = ServerRole::NEEDS_ELECTION;
                }
                break;
            case Type::ELECTION:
                handleElectionMessage(from_addr);
                break;
            // Outros tipos de mensagem (como REPLICATION_UPDATE) podem ser tratados aqui.
            default:
                break;
            }
        }
    }

    failure_detection_thread.join();
}

// Funções restantes (utilitárias, de impressão e de negócio)
// Esta é a versão da função com a formatação corrigida (sem caracteres invisíveis)
void Server::receiveNumbers()
{
    int numSocket = createSocket(this->client_request_port);
    if (numSocket == -1)
    {
        log_with_timestamp("[" + my_ip + "] Falha ao criar socket de requisições. A thread será encerrada.");
        return;
    }

    // O loop agora é infinito, pois esta thread serve tanto o LÍDER quanto o BACKUP.
    while (true)
    {
        Message number;
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        // Usar um timeout pequeno ou nenhuma flag de bloqueio é bom para não prender a thread
        int received = recvfrom(numSocket, &number, sizeof(Message), 0, (struct sockaddr *)&clientAddr, &clientLen);

        if (received > 0 && number.type == Type::REQ)
        {
            if (this->role == ServerRole::LEADER)
            {
                // LÓGICA DO LÍDER: Processa, replica e responde. (Como estava antes)
                string clientIP = inet_ntoa(clientAddr.sin_addr);
                if (isDuplicateRequest(clientIP, number.seq))
                {
                    printRepet(clientIP, number.seq);
                }
                else
                {
                    tableClient clientState = updateParticipant(clientIP, number.seq, number.num);
                    updateSumTable(number.seq, number.num);
                    printParticipants(clientIP);
                    tableAgregation server_state_copy;
                    {
                        lock_guard<mutex> lock(sumMutex);
                        server_state_copy = this->sumTotal;
                    }
                    replicateToBackups(number, clientAddr, clientState, server_state_copy);
                    Message confirmation = {Type::REQ_ACK, number.seq, clientState.last_req, clientState.last_sum};
                    sendto(numSocket, &confirmation, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);
                }
            }
            else
            {
                // LÓGICA DO BACKUP: Redireciona o cliente.
                log_with_timestamp("[" + my_ip + "] [BACKUP] Recebi REQ, redirecionando cliente para o líder " + this->leader_ip);
                Message redirect_msg = {Type::NOT_LEADER};
                if (!this->leader_ip.empty())
                {
                    inet_pton(AF_INET, this->leader_ip.c_str(), &redirect_msg.ip_addr);
                }
                sendto(numSocket, &redirect_msg, sizeof(Message), 0, (struct sockaddr *)&clientAddr, clientLen);
            }
        }
        else
        {
            // Pequena pausa para não sobrecarregar a CPU se não houver mensagens
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    }
    close(numSocket);
}

// As funções abaixo não precisaram de alterações na lógica principal de eleição
void Server::printInicio() { log_with_timestamp("num_reqs 0 total_sum 0"); }
void Server::handleClientDiscovery(const struct sockaddr_in &fromAddr)
{
    string clientIP = inet_ntoa(fromAddr.sin_addr);

    Message response = {Type::DESC_ACK, 0, 0};
    sendto(this->client_socket, &response, sizeof(Message), 0, (struct sockaddr *)&fromAddr, sizeof(fromAddr));

    lock_guard<mutex> lock(participantsMutex);
    if (!checkList(clientIP))
    {
        participants.push_back({clientIP, 0, 0, 0});
    }
}
bool Server::replicateToBackups(const Message &client_request, const struct sockaddr_in &client_addr, const tableClient &client_state, const tableAgregation &server_state)
{
    std::vector<ServerInfo> backups_to_notify;
    {
        lock_guard<mutex> lock(serverListMutex);
        if (server_list.empty())
        {
            cout << "nao tem ninguém" << endl;
            return true; // Sucesso, pois não há backups para replicar.
        }
        cout << "tem alguem hehehe" << endl;
        backups_to_notify = this->server_list;
    }

    log_with_timestamp("[" + my_ip + "] [LEADER] Iniciando replicação síncrona, um por um, para " + to_string(backups_to_notify.size()) + " backup(s)...");

    int replication_socket = createSocket(0); // Cria um socket sem bind, apenas para enviar/receber.
    if (replication_socket == -1)
    {
        log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Falha ao criar socket para replicação.");
        return false;
    }
    setSocketTimeout(replication_socket, 2); // Timeout de 2 segundos para cada resposta de backup.

    Message replication_msg = client_request;
    replication_msg.type = Type::REPLICATION_UPDATE;
    replication_msg.ip_addr = client_addr.sin_addr.s_addr;
    replication_msg.total_sum = client_state.last_sum;
    replication_msg.total_reqs = client_state.last_req;
    replication_msg.total_sum_server = server_state.sum;
    replication_msg.total_reqs_server = server_state.num_reqs;
    // setSocketTimeout(this->server_socket, 2); // Timeout de 2 segundos por resposta

    int successful_acks = 0;
    for (const auto &backup_info : backups_to_notify)
    {
        struct sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(this->server_communication_port);
        inet_pton(AF_INET, backup_info.ip_address.c_str(), &dest_addr.sin_addr);

        bool ack_received = false;
        // Envia a mensagem de replicação
        sendto(replication_socket, &replication_msg, sizeof(replication_msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

        // Tenta receber o ACK
        Message response;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recvfrom(replication_socket, &response, sizeof(response), 0, (struct sockaddr *)&from_addr, &from_len) > 0)
        {
            if (response.type == Type::REPLICATION_ACK && response.seq == client_request.seq)
            {
                ack_received = true;
            }
        }

        if (ack_received)
        {
            successful_acks++;
            log_with_timestamp("[" + my_ip + "] [LEADER] ACK recebido de " + backup_info.ip_address);
        }
        else
        {
            log_with_timestamp("[" + my_ip + "] [LEADER] ERRO: Backup " + backup_info.ip_address + " não respondeu. Pode estar offline.");
            // Aqui você poderia adicionar uma lógica para remover o backup da lista.
        }
    }

    close(replication_socket); // Fecha o socket temporário.
    return true;
}

void Server::setParticipantState(const std::string &clientIP, uint32_t seq, uint32_t value, uint64_t client_sum, uint32_t client_reqs)
{
    lock_guard<mutex> lock(participantsMutex);
    for (auto &p : participants)
    {
        if (p.address == clientIP)
        {
            p.last_sum = client_sum;
            p.last_req = client_reqs; // O backup agora armazena o mesmo que o líder
            p.last_value = value;
            return;
        }
    }
    participants.push_back({clientIP, client_reqs, client_sum, value});
}

void Server::printParticipants(const std::string &clientIP)
{
    lock_guard<mutex> lock_participants(participantsMutex);
    lock_guard<mutex> lock_sum(sumMutex);

    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            string msg = " client " + p.address +
                         " id_req " + to_string(p.last_req) +
                         " value " + to_string(p.last_value) +
                         " num_reqs " + to_string(sumTotal.num_reqs) +
                         " total_sum " + to_string(sumTotal.sum);
            log_with_timestamp(msg);
            return;
        }
    }
}

void Server::printRepet(const std::string &clientIP, uint32_t duplicate_seq)
{
    lock_guard<mutex> lock_participants(participantsMutex);
    lock_guard<mutex> lock_sum(sumMutex);

    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            string msg = " client " + p.address +
                         " DUP !! Tentativa de id_req " + to_string(duplicate_seq) +
                         ". Último id_req válido: " + to_string(p.last_req) +
                         " | num_reqs " + to_string(sumTotal.num_reqs) +
                         " total_sum " + to_string(sumTotal.sum);
            log_with_timestamp(msg);
            return;
        }
    }
}

bool Server::isDuplicateRequest(const string &clientIP, uint32_t seq)
{
    lock_guard<mutex> lock(participantsMutex);
    for (const auto &p : participants)
    {
        if (p.address == clientIP)
        {
            return (p.last_req == seq);
        }
    }
    return false;
}

bool Server::checkList(const std::string &ip)
{
    for (const auto &backup : server_list)
    {
        if (backup.ip_address == ip)
        {
            return true;
        }
    }
    return false;
}

tableClient Server::updateParticipant(const string &clientIP, uint32_t seq, uint32_t num)
{
    lock_guard<mutex> lock(participantsMutex);
    for (auto &p : participants)
    {
        if (p.address == clientIP)
        {
            p.last_sum += num;
            p.last_req = seq;
            p.last_value = num;
            return p;
        }
    }
    tableClient new_participant = {clientIP, seq, num, num};
    participants.push_back(new_participant);
    return new_participant;
}

void Server::updateSumTable(uint32_t seq, uint64_t num)
{
    lock_guard<mutex> lock(sumMutex);
    sumTotal.num_reqs++;
    sumTotal.sum += num;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        cerr << "Uso: " << argv[0] << " <porta_descoberta_cliente>" << endl;
        return 1;
    }
    int client_disc_port = atoi(argv[1]);
    int client_req_port = client_disc_port + 1;
    int server_comm_port = client_disc_port + 2;
    Server server(client_disc_port, client_req_port, server_comm_port);
    server.start();
    return 0;
}
