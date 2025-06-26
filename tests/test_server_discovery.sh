#!/bin/bash

# --- test_server_discovery.sh ---
# Este script testa a capacidade dos servidores de se descobrirem
# e assumirem os papéis de LÍDER e BACKUP corretamente.

echo "--- INICIANDO TESTE DE DESCOBERTA DE SERVIDORES ---"

# 1. Limpa o ambiente de execuções anteriores
echo "[PASSO 1] Limpando ambiente Docker..."
docker compose down --volumes

# 2. Constrói as imagens e inicia o PRIMEIRO servidor
echo "[PASSO 2] Construindo e iniciando 'server1'..."
docker compose up -d --build server1

echo "Aguardando 5 segundos para 'server1' se estabelecer..."
sleep 5

echo ""
echo "--------------------------------------------------"
echo "[VERIFICAÇÃO 1] Verifique os logs de 'server1'. Ele deve ter assumido o papel de LÍDER."
echo "Comando para ver os logs: docker compose logs server1"
echo "--------------------------------------------------"
echo ""


# 3. Inicia o SEGUNDO servidor
echo "[PASSO 3] Iniciando 'server2'..."
docker compose up -d server2

echo "Aguardando 5 segundos para 'server2' encontrar o líder..."
sleep 5

echo ""
echo "--------------------------------------------------"
echo "[VERIFICAÇÃO 2] Verifique os logs de 'server2'. Ele deve ter encontrado o líder e assumido o papel de BACKUP."
echo "Comando para ver os logs: docker compose logs server2"
echo "--------------------------------------------------"
echo ""

# 4. Finalização
echo "[PASSO 4] Teste concluído. O ambiente será limpo em 20 segundos."
sleep 20
docker compose down --volumes

echo "--- TESTE DE DESCOBERTA DE SERVIDORES FINALIZADO ---"