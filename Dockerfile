# Usando uma imagem base com C++ e gcc pré-instalados
FROM gcc:latest

# Definindo o diretório de trabalho dentro do container
WORKDIR /app

# Copiando o arquivo Makefile e o diretório src (com os arquivos .cpp) para dentro do container
COPY Makefile .
COPY src/ ./src/

# Instalando o make (se necessário)
RUN apt-get update && apt-get install -y make

# Compilando o código dentro do container
RUN make

# Definindo a execução com argumentos dinâmicos
ENTRYPOINT ["./server_client"]
