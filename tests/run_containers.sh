#!/bin/bash

# Limpa o ambiente de execuções anteriores
docker compose down --volumes

echo "Construindo e iniciando os contêineres..."
# Criar e iniciar os contêineres do servidor e clientes em modo detached (-d)
docker compose up -d --build

# Função para enviar números para um cliente específico
send_numbers_to_client() {
    local client_name=$1
    shift
    local numbers=("$@")

    echo "Enviando números para $client_name..."
    for num in "${numbers[@]}"; do
        # Abordagem corrigida: escreve diretamente no stdin do processo principal (PID 1)
        # que é o seu programa ./cliente.
        echo "$num" | docker compose exec -T "$client_name" sh -c 'cat > /proc/1/fd/0'
        sleep 0.5 # Uma pequena pausa para não sobrecarregar
    done
}

echo "Aguardando 3 segundos para os serviços estabilizarem..."
sleep 3

# --- INÍCIO DA SEÇÃO NOVA ---
echo ""
echo "--------------------------------------------------"
echo "--- IPs dos Contêineres ---"
# Mostra o IP de cada contêiner para fácil identificação nos logs
IP_SERVER=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' server)
IP_CLIENT1=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' client1)
IP_CLIENT2=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' client2)
IP_CLIENT3=$(docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' client3)

echo "IP do Servidor: $IP_SERVER"
echo "IP do Cliente 1: $IP_CLIENT1"
echo "IP do Cliente 2: $IP_CLIENT2"
echo "IP do Cliente 3: $IP_CLIENT3"
echo "--------------------------------------------------"
echo ""
# --- FIM DA SEÇÃO NOVA ---

# Enviar uma leva inicial de números
send_numbers_to_client client1 10 20
send_numbers_to_client client2 5
send_numbers_to_client client3 100 200 300

# --- INÍCIO DA SEÇÃO DE FALHAS COMENTADA ---
# echo ""
# echo "--------------------------------------------------"
# echo "SIMULANDO FALHA DO SERVIDOR..."
# echo "--------------------------------------------------"
# docker compose pause server
# echo "Servidor pausado por 15 segundos."

# # Tentar enviar números enquanto o servidor está fora (o cliente deve ficar tentando)
# send_numbers_to_client client1 33 44 & # O '&' roda em background

# sleep 15
# echo "RETOMANDO O SERVIDOR..."
# docker compose unpause server
# echo "Servidor voltou a rodar."

# sleep 10 # Aguarda o cliente 1 finalmente conseguir enviar os números 33 e 44

# echo "--------------------------------------------------"
# echo "SIMULANDO FALHA DO CLIENTE 2..."
# echo "--------------------------------------------------"
# docker compose pause client2
# echo "Cliente 2 pausado por 10 segundos."
# sleep 10
# docker compose unpause client2
# echo "Cliente 2 retomado."
# send_numbers_to_client client2 99 # Envia número após o cliente voltar
# --- FIM DA SEÇÃO DE FALHAS COMENTADA ---


echo ""
echo "--------------------------------------------------"
echo "Simulação do 'caminho feliz' concluída."
echo "Parando todos os contêineres em 15 segundos."
echo "--------------------------------------------------"
sleep 15
docker compose down --volumes

echo "Ambiente limpo. Teste finalizado."