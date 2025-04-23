# Nome dos executáveis
CLIENT_EXEC = client
SERVER_EXEC = server

# Compilador e flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Diretórios
SRC_DIR = src
OBJ_DIR = obj

# Fontes comuns
COMMON_SRC = $(SRC_DIR)/nodo.cpp

# Fontes específicos
CLIENT_SRC = $(SRC_DIR)/client.cpp
SERVER_SRC = $(SRC_DIR)/server.cpp

# Objetos
COMMON_OBJ = $(COMMON_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
CLIENT_OBJ = $(CLIENT_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
SERVER_OBJ = $(SERVER_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Regra principal
all: $(CLIENT_EXEC) $(SERVER_EXEC)

# Regras dos executáveis
$(CLIENT_EXEC): $(CLIENT_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SERVER_EXEC): $(SERVER_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compilar .cpp para .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Criação da pasta obj se não existir
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Limpeza
clean:
	rm -rf $(OBJ_DIR)/*.o $(CLIENT_EXEC) $(SERVER_EXEC)

# Rodar individualmente
run-client: $(CLIENT_EXEC)
	./$(CLIENT_EXEC)

run-server: $(SERVER_EXEC)
	./$(SERVER_EXEC)