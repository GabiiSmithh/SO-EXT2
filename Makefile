# Makefile para o projeto nEXT2 Shell

# --- Variáveis de Compilação ---

# O compilador C++
CXX = g++

# Flags do compilador:
# -std=c++17 : Usa o padrão C++17
# -Wall      : Ativa a maioria dos avisos (warnings)
# -Wextra    : Ativa avisos extras
# -g         : Inclui informações de depuração (para usar com gdb)
CXXFLAGS = -std=c++17 -Wall -Wextra -g

# Flags do linker:
# -lreadline : Liga (link) com a biblioteca readline para o terminal interativo
LDFLAGS = -lreadline

# --- Nomes dos Arquivos ---

# O nome do executável final
TARGET = next2shell

# Lista de todos os arquivos-fonte (.cpp) do projeto
SOURCES = main.cpp Ext2Shell.cpp

# Gera automaticamente a lista de arquivos-objeto (.o) a partir dos fontes
# Ex: main.cpp Ext2Shell.cpp se torna main.o Ext2Shell.o
OBJECTS = $(SOURCES:.cpp=.o)


# --- Regras (Targets) ---

# A primeira regra é a padrão, executada quando você digita "make"
# "all" é um nome convencional para a regra que constrói tudo.
# Ela depende do executável final (TARGET).
all: $(TARGET)

# Regra para ligar os arquivos-objeto e criar o executável final
# Depende de todos os arquivos .o listados em $(OBJECTS)
$(TARGET): $(OBJECTS)
	@echo "Ligando os arquivos para criar o executável: $(TARGET)"
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)
	@echo "Executável '$(TARGET)' criado com sucesso!"

# Regra de padrão para compilar arquivos .cpp em arquivos .o
# Diz ao make como transformar qualquer arquivo .cpp em seu .o correspondente.
%.o: %.cpp Ext2Shell.h nEXT2shell.h
	@echo "Compilando: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regra "clean": remove os arquivos gerados pela compilação
# Útil para forçar uma reconstrução completa do zero.
clean:
	@echo "Limpando arquivos gerados..."
	rm -f $(TARGET) $(OBJECTS)
	@echo "Limpeza concluída."

# Declara que 'all' e 'clean' são regras "falsas" (phony)
# Isso diz ao make que elas são apenas nomes de comandos e não arquivos reais.
.PHONY: all clean
