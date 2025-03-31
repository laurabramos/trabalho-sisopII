# Nome do executável
EXEC = server_client

# Compilador e flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall

# Diretórios
SRC_DIR = src
OBJ_DIR = obj

# Arquivos de código fonte
SRC = $(SRC_DIR)/server_client.cpp $(SRC_DIR)/nodo.cpp $(SRC_DIR)/server.cpp $(SRC_DIR)/client.cpp

# Arquivos de objetos
OBJ = $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Regra para compilar o projeto
all: $(EXEC)

# Regra para compilar o executável
$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Regra para compilar os arquivos .cpp para .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Limpeza dos arquivos gerados
clean:
	rm -rf $(OBJ_DIR)/*.o $(EXEC)

# Regra para criar os diretórios de objetos
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Para rodar o programa
run: $(EXEC)
	./$(EXEC)
