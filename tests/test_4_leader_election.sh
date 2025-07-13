#!/bin/bash

# --- test_staggered_start.sh ---
# Este script testa um cenário de inicialização escalonada, onde cada
# servidor é iniciado com um intervalo de 2 segundos.

# --- CONFIGURAÇÃO DO TERMINAL (IMPORTANTE!) ---
# Escolha o comando para o seu emulador de terminal.
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
        "konsole")        konsole --new-tab -p "title=$title" -e $full_command & ;;
        "xterm")          xterm -T "$title" -e "$full_command" & ;;
        *) echo "Terminal '$TERMINAL_EXECUTABLE' não suportado." ;;
    esac
}

echo "--- INICIANDO TESTE DE INICIALIZAÇÃO ESCALONADA ---"

# 1. Limpa o ambiente e recompila
echo "[PASSO 1] Limpando ambiente Docker e reconstruindo as imagens..."
docker compose down --volumes
docker compose build --no-cache

# 2. Inicia os servidores um a um com um intervalo de 2 segundos
echo ""
echo "[PASSO 2] Subindo servidores com um intervalo de 2 segundos..."
echo "   - Subindo 'server1'..."
docker compose up -d server1
sleep 2

echo "   - Subindo 'server2'..."
docker compose up -d server2
sleep 2

echo "   - Subindo 'server3'..."
docker compose up -d server3
sleep 2

echo "   - Subindo 'server4'..."
docker compose up -d server4


# 3. Abre os logs para cada servidor em um terminal separado
echo ""
echo "[PASSO 3] Abrindo janelas de log para cada servidor..."
open_terminal "Logs do Servidor 1" "docker compose logs -f server1"
open_terminal "Logs do Servidor 2" "docker compose logs -f server2"
open_terminal "Logs do Servidor 3" "docker compose logs -f server3"
open_terminal "Logs do Servidor 4" "docker compose logs -f server4"

echo ""
echo "--------------------------------------------------"
echo "[VERIFICAÇÃO] Observe as quatro janelas de terminal."
echo "O comportamento esperado agora é diferente:"
echo "1. 'server1' iniciará, não encontrará outros servidores e, após seu timeout de descoberta, se elegerá LÍDER."
echo "2. 'server2' iniciará 2 segundos depois, descobrirá que 'server1' já é o líder e se tornará um BACKUP."
echo "3. 'server3' e 'server4' seguirão o mesmo padrão, tornando-se BACKUPS do 'server1'."
echo "Neste cenário, uma eleição disputada não deve ocorrer."
echo ""
echo "Pressione [Enter] para encerrar o teste e limpar o ambiente..."
read
echo "--------------------------------------------------"
echo ""


# 4. Finalização
echo "[PASSO 4] Teste concluído. Encerrando e limpando o ambiente..."
docker compose down --volumes

echo "--- FIM DO TESTE ---"