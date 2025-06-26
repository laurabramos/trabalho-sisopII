# Estágio 1: Builder (usando Debian Bookworm como base, que usa o GCC 12)
FROM gcc:12 AS builder

# Define o diretório de trabalho
WORKDIR /app

# Copia o Makefile e o código-fonte para o contêiner
COPY Makefile .
COPY src/ ./src/

# Compila os executáveis 'cliente' e 'servidor' usando o Makefile
RUN make

# Estágio 2: Imagem Final (usando a mesma base, Debian Bookworm)
FROM debian:bookworm-slim

# Define o diretório de trabalho
WORKDIR /app

# Copia APENAS os executáveis compilados do estágio de 'builder'
COPY --from=builder /app/cliente .
COPY --from=builder /app/servidor .