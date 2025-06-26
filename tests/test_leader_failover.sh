#!/bin/bash

# --- test_leader_failover.sh ---
# Este script testa a estabilização inicial do sistema, a falha
# forçada do líder e a subsequente re-eleição de um novo líder.

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

echo "--- INICIANDO TESTE DE FALHA DO LÍDER (FAILOVER) ---"

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
echo "[VERIFICAÇÃO 1] ESTABILIZAÇÃO INICIAL"
echo "Observe as três janelas. O servidor com o MAIOR IP (provavelmente 'server3') deve se tornar LÍDER."
echo "Os outros dois devem se tornar BACKUPs. Aguarde o sistema se estabilizar (sem novas eleições)."
echo ""
echo "Pressione [Enter] quando o sistema estiver estável para FORÇAR A FALHA DO LÍDER..."
read
echo "--------------------------------------------------"
echo ""

# 4. Simula a falha do líder
# Assumimos que server3 é o mais provável a se tornar líder por ter o maior IP.
LEADER_CANDIDATE="server3"
echo "[PASSO 4] SIMULANDO FALHA DO LÍDER: Parando '$LEADER_CANDIDATE'..."
docker compose stop $LEADER_CANDIDATE
echo ">>> O líder ('$LEADER_CANDIDATE') está offline. Sua janela de log deve parar."
echo ">>> Os backups ('server1' e 'server2') devem detetar a falha e iniciar uma nova eleição."

# 5. Observação da re-eleição
echo ""
echo "--------------------------------------------------"
echo "[VERIFICAÇÃO 2] OBSERVE A RE-ELEIÇÃO"
echo "Acompanhe os logs de 'server1' e 'server2'."
echo "O servidor com o MAIOR IP entre os remanescentes (provavelmente 'server2') deve se anunciar como o novo LÍDER."
echo "O outro deve reconhecer o novo líder e continuar como backup."
echo ""
echo "Pressione [Enter] após verificar que um novo líder foi eleito e o sistema se estabilizou novamente..."
read
echo "--------------------------------------------------"
echo ""


# 6. Finalização
echo "[PASSO 5] Teste concluído. Encerrando e limpando o ambiente..."
docker compose down --volumes

echo "--- FIM DO TESTE DE FAILOVER ---"
