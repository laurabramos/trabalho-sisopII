#!/bin/bash

# --- test_client_leader_failover.sh ---
# Este script testa o ciclo completo de failover e recuperação do cliente em
# um ambiente com múltiplos servidores.
# 1. Inicia um cluster de 3 servidores, que elegem um líder.
# 2. Conecta um cliente, que deve encontrar o líder automaticamente.
# 3. Derruba o líder atual.
# 4. Verifica se os servidores restantes elegem um novo líder.
# 5. Verifica se o cliente consegue se reconectar ao NOVO líder e continuar operando.

echo "--- INICIANDO TESTE DE FAILOVER DE LÍDER E RECONEXÃO DO CLIENTE ---"

# Função para enviar números para um cliente específico
send_numbers_to_client() {
    local client_name=$1
    shift
    local numbers=("$@")

    echo ">>> Enviando números para $client_name..."
    for num in "${numbers[@]}"; do
        # Envia o número para o stdin do processo principal dentro do contêiner
        echo "$num" | docker compose exec -T "$client_name" sh -c 'cat > /proc/1/fd/0'
        sleep 0.5
    done
}

# 1. Limpa o ambiente de execuções anteriores
echo "[PASSO 1] Limpando ambiente Docker..."
docker compose down --volumes

# 2. Constrói e inicia TODOS os servidores e o cliente
echo "[PASSO 2] Construindo e iniciando 'server1', 'server2', 'server3' e 'client1'..."
docker compose up -d --build server1 server2 server3 client1

echo "Aguardando 10 segundos para a eleição inicial e estabilização dos serviços..."
sleep 10

# 3. Verifica a conexão inicial com o líder eleito
echo "[PASSO 3] Enviando números para testar a conexão inicial..."
send_numbers_to_client client1 10 20
echo ">>> Conexão inicial com o líder bem-sucedida (verifique os logs do líder, provavelmente 'server3')."
sleep 3

# --- INÍCIO DA LÓGICA DE FALHA E RECUPERAÇÃO ---

# 4. Simula a falha do líder
# Assumimos que 'server3' é o líder inicial, pois tem o maior IP no compose.
LEADER_TO_KILL="server3"
echo ""
echo "--------------------------------------------------"
echo "[PASSO 4] SIMULANDO FALHA DO LÍDER ATUAL ($LEADER_TO_KILL)..."
docker compose stop $LEADER_TO_KILL
echo ">>> O líder ficará offline. Uma nova eleição deve começar entre 'server1' e 'server2'."
echo ">>> O cliente deve tentar enviar o próximo número, falhar e entrar no loop de redescoberta."
echo "--------------------------------------------------"
echo ""

# Tenta enviar um número com o líder offline para FORÇAR o cliente a detectar a falha.
# Roda em background (&) para não travar o script.
send_numbers_to_client client1 99 &

# Espera tempo suficiente para a re-eleição e para o cliente tentar reenviar.
echo "[PASSO 5] Aguardando 15 segundos para a re-eleição e para o cliente encontrar o novo líder..."
sleep 15

# 5. Teste final de reconexão ao NOVO líder
# O novo líder agora deve ser 'server2'.
echo ""
echo "--------------------------------------------------"
echo "[PASSO 6] Enviando um número final para validar a reconexão com o NOVO líder..."
send_numbers_to_client client1 777
echo ">>> Verifique os logs do NOVO LÍDER (provavelmente 'server2')."
echo ">>> Se o número '777' foi processado, o teste foi um SUCESSO!"
echo "--------------------------------------------------"
echo ""

# 6. Finalização
echo "[PASSO 7] Teste concluído. O ambiente será limpo em 15 segundos."
sleep 15
docker compose down --volumes

echo "--- TESTE DE RESILIÊNCIA E RECONEXÃO FINALIZADO ---"
