#!/bin/bash

unset GTK_PATH

# Função para iniciar um contêiner
start_service() {
    docker compose up -d "$1"
}

# Função para parar um contêiner
stop_service() {
    docker compose stop "$1"
}

# Função para suspender um contêiner
suspend_service() {
    docker compose pause "$1"
}

# Função para retomar um contêiner suspenso
wake_service() {
    docker compose unpause "$1"
}

# Função para monitorar logs dos serviços
monitor_logs() {
    gnome-terminal --geometry=100x30 -- bash -c "docker compose logs -f server; exec bash"
    gnome-terminal --geometry=100x30 -- bash -c "docker compose logs -f client1; exec bash"
    gnome-terminal --geometry=100x30 -- bash -c "docker compose logs -f client2; exec bash"
    gnome-terminal --geometry=100x30 -- bash -c "docker compose logs -f client3; exec bash"
}

# Criar e iniciar os contêineres do servidor e clientes
docker compose up -d

# Monitorar os logs dos serviços
monitor_logs

# Aguardar até que o servidor esteja pronto
sleep 10

# Simular o desligamento e recuperação do líder (servidor)
suspend_service server
sleep 15

wake_service server
echo "Servidor voltou a rodar."

# Simular queda de um cliente
suspend_service client1
echo "Cliente 1 suspenso."
sleep 10

wake_service client1
echo "Cliente 1 retomado."

# Parar todos os contêineres após o teste
sleep 30
docker compose down

echo "Todos os contêineres foram parados."
