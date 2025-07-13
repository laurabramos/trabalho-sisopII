#!/bin/bash

# --- interactive_failover_test.sh ---
# Testa a replicação e o failover de forma interativa.
# - Inicia 2 servidores e 2 clientes.
# - Permite enviar números um a um, alternando entre os clientes.
# - O usuário pode derrubar o líder a qualquer momento para observar a recuperação.

# --- CONFIGURAÇÃO DO TERMINAL (IMPORTANTE!) ---
TERMINAL_EXECUTABLE="gnome-terminal"

# --- Função para abrir um novo terminal e executar um comando ---
open_terminal() {
    local title="$1"
    local command_to_run="$2"

    if ! command -v $TERMINAL_EXECUTABLE &> /dev/null; then
        echo "AVISO: O emulador de terminal '$TERMINAL_EXECUTABLE' não foi encontrado."
        echo "Use 'docker compose logs -f' para monitorar os logs manualmente."
        return
    fi

    local full_command="/bin/sh -c \"${command_to_run}; echo; echo '---'; read -p 'Pressione [Enter] para fechar...'\""

    case $TERMINAL_EXECUTABLE in
        "gnome-terminal") gnome-terminal --title="$title" -- $full_command & ;;
        "konsole")        konsole --new-tab -p "title=$title" -e "$full_command" & ;;
        "xterm")          xterm -T "$title" -e "$full_command" & ;;
        *) echo "Terminal '$TERMINAL_EXECUTABLE' não suportado." ;;
    esac
}

# --- Função para enviar um ÚNICO número para um cliente ---
send_number_to_client() {
    local client_name=$1
    local number_to_send=$2

    echo ">>> Enviando o número '$number_to_send' para $client_name..."
    # Envia o número para o stdin do processo principal dentro do contêiner
    echo "$number_to_send" | docker compose exec -T "$client_name" sh -c 'cat > /proc/1/fd/0'
}

echo "--- INICIANDO TESTE INTERATIVO DE REPLICAÇÃO E FAILOVER ---"

# 1. Limpeza e Build
echo "[PASSO 1] Limpando e reconstruindo o ambiente Docker..."
docker compose down --volumes
docker compose build --no-cache

# 2. Iniciar 2 servidores e 2 clientes
echo ""
echo "[PASSO 2] Subindo servidores com um intervalo de 2 segundos..."
echo "   - Subindo 'server1'..."
docker compose up -d server1
#sleep 5

echo "   - Subindo 'server2'..."
docker compose up -d server2
#sleep 5

echo "   - Subindo 'server3'..."
docker compose up -d server3
#sleep 5

echo "   - Subindo 'server4'..."
docker compose up -d server4

echo "[PASSO 2] Subindo 'client1' e 'client2'..."
# Usamos o `docker-compose.yml` que já tem os 3 servidores, mas só iniciamos os 2 primeiros.
docker compose up -d client1 client2

# 3. Abrir logs
echo "[PASSO 3] Abrindo janelas de log para todos os serviços..."
open_terminal "Logs do Servidor 1" "docker compose logs -f server1"
open_terminal "Logs do Servidor 2" "docker compose logs -f server2"
open_terminal "Logs do Cliente 1" "docker compose logs -f client1"
open_terminal "Logs do Cliente 2" "docker compose logs -f client2"

echo ""
echo "Aguardando 10 segundos para a eleição do líder inicial..."
sleep 10
echo ""
echo "--------------------------------------------------"
echo "AMBIENTE PRONTO. Um líder deve ter sido eleito."
echo "Você pode agora enviar números um a um para cada cliente."
echo "Observe nos logs a replicação do estado para o servidor backup."
echo ""
echo "A QUALQUER MOMENTO, você pode parar o contêiner do LÍDER (ex: 'docker compose stop server2')"
echo "e continuar o teste para observar o failover e a reconexão dos clientes."
echo "--------------------------------------------------"
echo ""

# 4. Loop interativo para envio de números
counter=1
current_client="client1"

while true; do
    read -p "Pressione [Enter] para enviar o número $counter para o $current_client (ou 'q' para sair)..." input
    if [[ "$input" == "q" ]]; then
        break
    fi

    send_number_to_client "$current_client" "$counter"

    # Alterna o cliente para o próximo envio
    if [[ "$current_client" == "client1" ]]; then
        current_client="client2"
    else
        current_client="client1"
    fi

    counter=$((counter + 1))
    echo "--------------------------------------------------"
done

# 5. Finalização
echo "[PASSO 5] Teste interativo concluído. Limpando o ambiente..."
docker compose down --volumes

echo "--- FIM DO TESTE ---"
