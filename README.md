# Trabalho Sisop II
 Trabalho da disciplina de sistemas operacionais II, UFRGS, 25/1

## Comandos para instalar o docker

Instalar o docker

sudo snap install docker

sudo groupadd docker

sudo usermod -aG docker $USER

newgrp docker

docker run hello-world reboot

## Rodar o docker

docker build -t server_client .

docker-compose up

docker-compose down

## Rodar bash 

./run_containers.sh
