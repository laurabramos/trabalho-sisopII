
# Trabalho Sisop II

Trabalho da disciplina de Sistemas Operacionais II (INF01151), UFRGS, semestre 2025/1.

O projeto consiste em um serviço distribuído de soma de números, implementado em C++ com funcionalidades de replicação passiva e eleição de líder para garantir alta disponibilidade e tolerância a falhas.

## 1. Pré-requisitos

É necessário ter o **Docker** e o **Docker Compose** instalados.

### Instalação do Docker no Ubuntu/Debian

Para a maioria das distribuições baseadas em Debian (como o Ubuntu), o método recomendado é via repositório oficial. Para outras distribuições, consulte a [documentação oficial do Docker](https://docs.docker.com/engine/install/ "null").

```
# 1. Desinstalar versões antigas
sudo apt-get remove docker docker-engine docker.io containerd runc

# 2. Instalar dependências
sudo apt-get update
sudo apt-get install -y ca-certificates curl gnupg

# 3. Adicionar a chave GPG oficial do Docker
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL [https://download.docker.com/linux/ubuntu/gpg](https://download.docker.com/linux/ubuntu/gpg) | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
sudo chmod a+r /etc/apt/keyrings/docker.gpg

# 4. Adicionar o repositório do Docker
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] [https://download.docker.com/linux/ubuntu](https://download.docker.com/linux/ubuntu) \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt-get update

# 5. Instalar o Docker Engine, CLI, Containerd e Docker Compose
sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

```

### Configuração do Docker (Pós-instalação)

Para rodar o Docker sem precisar usar `sudo` a cada comando:

```
# 1. Criar o grupo 'docker' (geralmente já existe)
sudo groupadd docker

# 2. Adicionar seu usuário ao grupo 'docker'
sudo usermod -aG docker $USER

# 3. ATENÇÃO: É necessário fazer logout e login novamente (ou reiniciar o computador)
# para que a mudança de grupo tenha efeito.
# Para aplicar a mudança na sessão atual do terminal, execute:
newgrp docker

```

### Verificar a Instalação

Para confirmar que tudo foi instalado corretamente, rode o "hello-world" do Docker:

```
docker run hello-world

```

## 2. Como Compilar e Executar o Projeto

Com o Docker configurado, use o Docker Compose para orquestrar a compilação e a execução dos contêineres.

```
# Para construir as imagens e iniciar todos os serviços (servidor e clientes)
# O comando '--build' garante que o código será recompilado se houver mudanças.
docker compose up --build

# Para rodar em modo "detached" (em segundo plano)
docker compose up --build -d

# Para parar e remover os contêineres
docker compose down

```

## 3. Scripts de Teste Automatizados

O projeto inclui scripts de shell (`.sh`) para automatizar testes de funcionalidades específicas.

**Para executar um script, primeiro dê a ele permissão de execução:**

```
chmod +x nome_do_script.sh

```

**E então execute-o:**

```
./nome_do_script.sh

```

Cada script é projetado para criar um cenário, executar ações e depois limpar o ambiente, facilitando a validação de cada parte do sistema.
