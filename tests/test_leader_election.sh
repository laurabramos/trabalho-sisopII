#!/bin/bash

# --- test_all_servers_simultaneously.sh ---
# Este script testa o cenário mais caótico: todos os três servidores
# são iniciados ao mesmo tempo, forçando uma eleição imediata.

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

echo "--- INICIANDO TESTE DE ELEIÇÃO COM TRÊS SERVIDORES SIMULTÂNEOS ---"

# 1. Limpa o ambiente e recompila
echo "[PASSO 1] Limpando ambiente Docker e reconstruindo as imagens..."
docker compose down --volumes
docker compose build --no-cache

# 2. Inicia TODOS os servidores ao mesmo tempo
echo ""
echo "[PASSO 2] Subindo 'server1', 'server2' e 'server3' simultaneamente..."
docker compose up -d server1 server2 server3

# 3. Abre os logs para cada servidor em um terminal separado
echo "[PASSO 3] Abrindo janelas de log para cada servidor..."
open_terminal "Logs do Servidor 1" "docker compose logs -f server1"
open_terminal "Logs do Servidor 2" "docker compose logs -f server2"
open_terminal "Logs do Servidor 3" "docker compose logs -f server3"

echo ""
echo "--------------------------------------------------"
echo "[VERIFICAÇÃO] Observe as três janelas de terminal."
echo "Os servidores entrarão em eleição. O servidor com o MAIOR IP deve se tornar o LÍDER."
echo "Os outros dois devem reconhecer o líder e se tornarem BACKUP."
echo "Após a eleição, o sistema deve se estabilizar."
echo ""
echo "Pressione [Enter] para encerrar o teste e limpar o ambiente..."
read
echo "--------------------------------------------------"
echo ""


# 4. Finalização
echo "[PASSO 4] Teste concluído. Encerrando e limpando o ambiente..."
docker compose down --volumes

echo "--- FIM DO TESTE ---"