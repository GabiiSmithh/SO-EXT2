#include "Ext2Shell.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <algorithm>

// Construtor: Abre a imagem e inicializa o estado
Ext2Shell::Ext2Shell(const std::string& imagePath) : imagePath(imagePath), fd(-1) {
    fd = open(imagePath.c_str(), O_RDWR);
    if (fd < 0) {
        throw std::runtime_error("Error: Could not open image file '" + imagePath + "'.");
    }
    initialize();
}

// Destrutor: Fecha o arquivo
Ext2Shell::~Ext2Shell() {
    if (fd >= 0) {
        close(fd);
    }
}

// Inicialização: Lê o superbloco e o inode raiz
void Ext2Shell::initialize() {
    lseek(fd, BASE_OFFSET, SEEK_SET);
    read(fd, &super, sizeof(ext2_super_block));

    if (super.s_magic != EXT2_SUPER_MAGIC) {
        throw std::runtime_error("Error: Not a valid EXT2 filesystem.");
    }
    
    blockSize = 1024 << super.s_log_block_size;
    currentGroupNum = 0;
    
    updateCurrentDirectory(EXT2_ROOT_INO); // O inode raiz é o 2
}

// Atualiza o estado interno para um novo diretório
void Ext2Shell::updateCurrentDirectory(unsigned int inodeNum) {
    currentInodeNum = inodeNum;
    currentGroupNum = (inodeNum - 1) / super.s_inodes_per_group;
    readGroupDesc(currentGroupNum, &currentGroupDesc);
    readInode(currentInodeNum, &currentInode);
}

// --- Métodos de Baixo Nível ---

inline unsigned int block_offset(unsigned int block, unsigned int blockSize) {
    return BASE_OFFSET + (block - 1) * blockSize;
}

void Ext2Shell::readBlock(unsigned int block, void* buffer) {
    lseek(fd, block_offset(block, blockSize), SEEK_SET);
    read(fd, buffer, blockSize);
}

void Ext2Shell::writeBlock(unsigned int block, const void* buffer) {
    lseek(fd, block_offset(block, blockSize), SEEK_SET);
    write(fd, buffer, blockSize);
}

void Ext2Shell::readGroupDesc(unsigned int groupNum, ext2_group_desc* group) {
    lseek(fd, BASE_OFFSET + blockSize + groupNum * sizeof(ext2_group_desc), SEEK_SET);
    read(fd, group, sizeof(ext2_group_desc));
}

void Ext2Shell::writeGroupDesc(unsigned int groupNum, const ext2_group_desc* group) {
    lseek(fd, BASE_OFFSET + blockSize + groupNum * sizeof(ext2_group_desc), SEEK_SET);
    write(fd, group, sizeof(struct ext2_group_desc));
}

void Ext2Shell::readInode(unsigned int inodeNum, ext2_inode* inode) {
    unsigned int group = (inodeNum - 1) / super.s_inodes_per_group;
    ext2_group_desc groupDesc;
    readGroupDesc(group, &groupDesc);

    unsigned int index = (inodeNum - 1) % super.s_inodes_per_group;
    unsigned int offset = groupDesc.bg_inode_table * blockSize + index * sizeof(ext2_inode);
    lseek(fd, BASE_OFFSET - 1024 + offset, SEEK_SET);
    read(fd, inode, sizeof(ext2_inode));
}

void Ext2Shell::writeInode(unsigned int inodeNum, const ext2_inode* inode) {
    unsigned int group = (inodeNum - 1) / super.s_inodes_per_group;
    ext2_group_desc groupDesc;
    readGroupDesc(group, &groupDesc);

    unsigned int index = (inodeNum - 1) % super.s_inodes_per_group;
    unsigned int offset = groupDesc.bg_inode_table * blockSize + index * sizeof(ext2_inode);
    lseek(fd, BASE_OFFSET - 1024 + offset, SEEK_SET);
    write(fd, inode, sizeof(ext2_inode));
}


// --- Lógica do Shell ---

void Ext2Shell::run() {
    std::string line;
    std::cout << "nEXT2 Shell initialized. Type 'exit' to quit." << std::endl;
    while (true) {
        std::cout << getPrompt();
        if (!std::getline(std::cin, line) || line == "exit") {
            break;
        }
        if (!line.empty()) {
            processCommand(line);
        }
    }
    std::cout << "Exiting shell." << std::endl;
}

std::string Ext2Shell::getPrompt() const {
    std::string pathStr = "/";
    for (const auto& part : currentPath) {
        pathStr += part + "/";
    }
    if (pathStr.length() > 1) {
        pathStr.pop_back(); // Remove a última /
    }
    return "[" + pathStr + "]$> ";
}

// Ler palavras que tem espaço com "" ou com '\'
std::vector<std::string> Ext2Shell::tokenize(const std::string& input) {
    std::vector<std::string> tokens;  // Vetor para armazenar os tokens extraídos
    std::string current;              // Acumulador para o token atual
    bool inQuotes = false;            // Flag para verificar se estamos dentro de aspas
    bool escaping = false;            // Flag para verificar se o próximo caractere é escapado

    for (char c : input) {
        if (escaping) {
            // Se o caractere anterior era '\', adiciona este como literal
            current += c;
            escaping = false; // Consome o escape
        } else if (c == '\\') {
            // Inicia escape — o próximo caractere será tratado literalmente
            escaping = true;
        } else if (c == '"') {
            // Alterna o estado de 'inQuotes' ao encontrar aspas
            inQuotes = !inQuotes;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !inQuotes) {
            // Se for espaço e não estamos dentro de aspas, fecha o token atual
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            // Adiciona caractere normalmente ao token atual
            current += c;
        }
    }

    // Após o loop, adiciona o último token
    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}


void Ext2Shell::processCommand(const std::string& line) {
    std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) return;

    std::string command = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    try {
        if (command == "info") cmd_info();
        else if (command == "ls") cmd_ls();
        else if (command == "pwd") cmd_pwd();
        else if (command == "cd" && args.size() == 1) cmd_cd(args[0]);
        else if (command == "attr" && args.size() == 1) cmd_attr(args[0]);
        else if (command == "cat" && args.size() == 1) cmd_cat(args[0]);
        else if (command == "touch" && args.size() == 1) cmd_touch(args[0]);
        else if (command == "mkdir" && args.size() == 1) cmd_mkdir(args[0]);
        else if (command == "rm" && args.size() == 1) cmd_rm(args[0]);
        else if (command == "rmdir" && args.size() == 1) cmd_rmdir(args[0]);
        else if (command == "cp" && args.size() == 2) cmd_cp(args[0], args[1]);
        else if (command == "rename" && args.size() == 2) cmd_rename(args[0], args[1]);
        else if (command.empty()) { /* Faz nada */ }
        else std::cerr << "Error: Unknown command or incorrect arguments." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
    }
}

// --- Implementações dos Comandos ---

void Ext2Shell::cmd_info() {
    std::cout << "Volume name.....: " << super.s_volume_name << std::endl;
    std::cout << "Image size......: " << (super.s_blocks_count * blockSize) / 1024 << " KiB" << std::endl;
    std::cout << "Free space......: " << (super.s_free_blocks_count * blockSize) / 1024 << " KiB" << std::endl;
    std::cout << "Free inodes.....: " << super.s_free_inodes_count << std::endl;
    std::cout << "Block size......: " << blockSize << " bytes" << std::endl;
    std::cout << "Groups count....: " << (super.s_blocks_count / super.s_blocks_per_group) << std::endl;
}

// Percorre as entradas de um diretório e chama um callback para cada uma
void Ext2Shell::forEachDirEntry(unsigned int dirInodeNum, std::function<bool(ext2_dir_entry_2*)> callback) {
    ext2_inode dirInode;
    readInode(dirInodeNum, &dirInode);
    if (!S_ISDIR(dirInode.i_mode)) return;

    forEachDataBlock(dirInodeNum, [&](const std::vector<char>& block_data) {
        unsigned int offset = 0;
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&block_data[offset];
            if (entry->inode == 0) break;

            if (!callback(entry)) {
                // O callback retornou false, para a iteração
                // (Isso é um truque para poder parar a iteração quando encontramos o que queremos)
                // TODO: Encontrar uma forma melhor de parar o forEachDataBlock
            }

            offset += entry->rec_len;
        }
    });
}

// Retorna o inode de um arquivo/dir pelo nome no diretório atual
unsigned int Ext2Shell::getInodeByName(const std::string& name) {
    unsigned int foundInode = 0;
    forEachDirEntry(currentInodeNum, [&](ext2_dir_entry_2* entry) {
        std::string entryName(entry->name, entry->name_len);
        if (entryName == name) {
            foundInode = entry->inode;
            return false; // Para a iteração
        }
        return true; // Continua
    });
    return foundInode;
}

void Ext2Shell::forEachDataBlock(unsigned int inodeNum, std::function<void(const std::vector<char>&)> callback) {
    ext2_inode inode;
    readInode(inodeNum, &inode);

    // vetor temporário para o bloco lido
    std::vector<char> buffer(blockSize);

    // Blocos diretos (12 primeiros)
    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] == 0) continue;
        readBlock(inode.i_block[i], buffer.data());
        callback(buffer);
    }
}

// Função para adicionar uma entrada de diretório no diretório pai
int Ext2Shell::addDirectoryEntry(unsigned int parentInodeNum, unsigned int childInodeNum, const std::string& name, unsigned char fileType) {
    ext2_inode parentInode;
    // Lê o inode do diretório pai
    readInode(parentInodeNum, &parentInode);

    std::vector<char> blockData(blockSize);

    // Percorre os 12 blocos diretos do inode pai
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == 0) continue;  // bloco vazio, pula

        // Lê o bloco do diretório
        readBlock(parentInode.i_block[i], blockData.data());

        unsigned int offset = 0;
        // Varre as entradas do diretório dentro do bloco
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];

            // Calcula o tamanho ideal da entrada atual, alinhado a 4 bytes
            unsigned int idealLen = 8 + ((entry->name_len + 3) & ~3);
            // Espaço sobrando após a entrada atual (potencial para nova entrada)
            unsigned int leftover = entry->rec_len - idealLen;
            // Tamanho necessário para a nova entrada, alinhado a 4 bytes
            unsigned int neededLen = 8 + ((name.size() + 3) & ~3);

            // Verifica se o espaço sobrando é suficiente para a nova entrada
            if (leftover >= neededLen) {
                // Ajusta o tamanho da entrada atual para o ideal, liberando espaço
                entry->rec_len = idealLen;

                // Cria a nova entrada no espaço liberado
                ext2_dir_entry_2* newEntry = (ext2_dir_entry_2*)&blockData[offset + idealLen];
                newEntry->inode = childInodeNum;
                newEntry->rec_len = leftover;
                newEntry->name_len = name.size();
                newEntry->file_type = fileType;
                memcpy(newEntry->name, name.c_str(), name.size());

                // Escreve o bloco atualizado no disco
                writeBlock(parentInode.i_block[i], blockData.data());

                // Incrementa o contador de links do diretório pai SE a entrada for um diretório
                if (fileType == EXT2_FT_DIR) {
                    parentInode.i_links_count++;
                    writeInode(parentInodeNum, &parentInode);
                }

                return 0; // sucesso
            }

            // Avança para a próxima entrada no bloco
            offset += entry->rec_len;
        }
    }

    // Retorna erro se não houver espaço suficiente em nenhum bloco
    return -1;
}

// Remove apenas a Directory Entry sem liberar o inode
void Ext2Shell::removeDirectoryEntry(unsigned int parentInodeNum, const std::string& name) {
    ext2_inode parentInode;
    readInode(parentInodeNum, &parentInode);

    std::vector<char> blockData(blockSize);
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == 0)
            continue;
        readBlock(parentInode.i_block[i], blockData.data());

        ext2_dir_entry_2* prev = nullptr;
        unsigned int offset = 0;
        while (offset < blockSize) {
            auto* e = reinterpret_cast<ext2_dir_entry_2*>(&blockData[offset]);
            if (e->inode == 0) 
                break;

            std::string entryName(e->name, e->name_len);
            if (entryName == name) {
                if (prev) {
                    // junta rec_len
                    prev->rec_len += e->rec_len;
                } else {
                    // se for primeiro, marca inode=0 (entry “vazia”)
                    e->inode = 0;
                }
                writeBlock(parentInode.i_block[i], blockData.data());
                return;
            }
            prev = e;
            offset += e->rec_len;
        }
    }
}

// Verifica se um bit está marcado no bitmap
bool Ext2Shell::isBitSet(unsigned char* bitmap, int bit) {
    int bytePos = bit / 8;
    int bitPos = bit % 8;
    return (bitmap[bytePos] & (1 << bitPos)) != 0;
}
// Procura um inode livre no bitmap do grupo atual e retorna o número do inode ou -1 se não achar
int Ext2Shell::findFreeInode() {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap); // lê bitmap do inode do grupo atual

    for (unsigned int i = 0; i < super.s_inodes_per_group; i++) {
        int bytePos = i / 8;           // índice do byte no bitmap
        int bitPos = i % 8;            // posição do bit dentro do byte
        if (!(bitmap[bytePos] & (1 << bitPos))) { // se bit não está setado, inode livre
            // Retorna o número do inode considerando grupo e offset dentro do grupo
            return i + 1 + currentGroupNum * super.s_inodes_per_group;
        }
    }
    return -1; // Nenhum inode livre encontrado
}

// Procura um bloco livre no bitmap do grupo atual e retorna o número do bloco ou -1 se não achar
int Ext2Shell::findFreeBlock() {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap); // lê bitmap dos blocos do grupo atual

    for (unsigned int i = 0; i < super.s_blocks_per_group; i++) {
        int bytePos = i / 8;           // índice do byte no bitmap
        int bitPos = i % 8;            // posição do bit dentro do byte
        if (!(bitmap[bytePos] & (1 << bitPos))) { // se bit não está setado, bloco livre
            // Retorna o número do bloco considerando grupo e offset dentro do grupo
            return i + 1 + currentGroupNum * super.s_blocks_per_group;
        }
    }
    return -1; // Nenhum bloco livre encontrado
}

// Aloca um inode: marca bit no bitmap, atualiza contadores, retorna número do inode alocado
int Ext2Shell::allocateInode() {
    int inodeNum = findFreeInode(); // procura inode livre
    if (inodeNum < 0) return -1;    // não encontrou inode livre

    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap); // lê bitmap do grupo

    int bit = (inodeNum - 1) % super.s_inodes_per_group; // bit relativo ao grupo
    int bytePos = bit / 8;
    int bitPos = bit % 8;

    bitmap[bytePos] |= (1 << bitPos);  // marca bit como ocupado
    writeBlock(currentGroupDesc.bg_inode_bitmap, bitmap); // salva bitmap atualizado

    // Atualiza contadores de inodes livres no superbloco e no grupo
    super.s_free_inodes_count--;
    currentGroupDesc.bg_free_inodes_count--;

    // Escreve superbloco atualizado no disco (offset fixo)
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));

    // Atualiza o grupo no disco
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    return inodeNum; // retorna número do inode alocado
}

// Aloca um bloco: marca bit no bitmap, atualiza contadores, retorna número do bloco alocado
int Ext2Shell::allocateBlock() {
    int blockNum = findFreeBlock(); // procura bloco livre
    if (blockNum < 0) return -1;    // não encontrou bloco livre

    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap); // lê bitmap do grupo

    int bit = (blockNum - 1) % super.s_blocks_per_group; // bit relativo ao grupo
    int bytePos = bit / 8;
    int bitPos = bit % 8;

    bitmap[bytePos] |= (1 << bitPos);  // marca bit como ocupado
    writeBlock(currentGroupDesc.bg_block_bitmap, bitmap); // salva bitmap atualizado

    // Atualiza contadores de blocos livres no superbloco e no grupo
    super.s_free_blocks_count--;
    currentGroupDesc.bg_free_blocks_count--;

    // Escreve superbloco atualizado no disco (offset fixo)
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));

    // Atualiza o grupo no disco
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    return blockNum; // retorna número do bloco alocado
}

// Libera um inode: limpa bit no bitmap e atualiza contadores
void Ext2Shell::freeInode(unsigned int inodeNum) {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap); // lê bitmap do grupo

    int bit = (inodeNum - 1) % super.s_inodes_per_group; // bit relativo ao grupo
    int bytePos = bit / 8;
    int bitPos = bit % 8;

    bitmap[bytePos] &= ~(1 << bitPos);  // limpa bit (marca inode livre)
    writeBlock(currentGroupDesc.bg_inode_bitmap, bitmap); // salva bitmap atualizado

    // Atualiza contadores de inodes livres no superbloco e no grupo
    super.s_free_inodes_count++;
    currentGroupDesc.bg_free_inodes_count++;

    // Escreve superbloco atualizado no disco (offset fixo)
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));

    // Atualiza o grupo no disco
    writeGroupDesc(currentGroupNum, &currentGroupDesc);
}

// Libera um bloco: limpa bit no bitmap e atualiza contadores
void Ext2Shell::freeBlock(unsigned int blockNum) {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap); // lê bitmap do grupo

    int bit = (blockNum - 1) % super.s_blocks_per_group; // bit relativo ao grupo
    int bytePos = bit / 8;
    int bitPos = bit % 8;

    bitmap[bytePos] &= ~(1 << bitPos);  // limpa bit (marca bloco livre)
    writeBlock(currentGroupDesc.bg_block_bitmap, bitmap); // salva bitmap atualizado

    // Atualiza contadores de blocos livres no superbloco e no grupo
    super.s_free_blocks_count++;
    currentGroupDesc.bg_free_blocks_count++;

    // Escreve superbloco atualizado no disco (offset fixo)
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));

    // Atualiza o grupo no disco
    writeGroupDesc(currentGroupNum, &currentGroupDesc);
}


void Ext2Shell::cmd_ls() {
    forEachDirEntry(currentInodeNum, [](ext2_dir_entry_2* entry) {
        std::string name(entry->name, entry->name_len);
        std::cout << name << std::endl;
        std::cout << "inode: " << entry->inode << std::endl;
        std::cout << "record lenght: " << entry->rec_len << std::endl;
        std::cout << "name lenght: " << static_cast<int>(entry->name_len) << std::endl;
        std::cout << "file type: " << static_cast<int>(entry->file_type) << std::endl;
        std::cout << std::endl;
        return true;
    });
}

void Ext2Shell::cmd_cd(const std::string& path) {
    if (path == "..") {
        if (!currentPath.empty()) {
            currentPath.pop_back();
            // Refaz o caminho desde a raiz para encontrar o inode
            unsigned int newInodeNum = EXT2_ROOT_INO;
            // salva o estado atual
            auto tempPath = currentPath; 
            currentPath.clear();
            updateCurrentDirectory(newInodeNum);
            // navega até o novo diretório
            for(const auto& dir : tempPath){
                cmd_cd(dir);
            }
        }
        return;
    }
    
    if (path == ".") return;

    unsigned int inodeNum = getInodeByName(path);
    if (inodeNum == 0) {
        std::cerr << "Error: Directory '" << path << "' not found." << std::endl;
        return;
    }

    ext2_inode newInode;
    readInode(inodeNum, &newInode);
    if (!S_ISDIR(newInode.i_mode)) {
        std::cerr << "Error: '" << path << "' is not a directory." << std::endl;
        return;
    }

    updateCurrentDirectory(inodeNum);
    currentPath.push_back(path);
}

void Ext2Shell::cmd_pwd() {
    std::cout << getPrompt() << std::endl;
}

// ... (Implementação dos outros comandos: cat, attr, touch, mkdir, etc.)
// A implementação completa seria muito longa, mas a estrutura acima dá a base.
// O segredo está em usar os métodos auxiliares (getInodeByName, forEachDataBlock, etc.)
// para construir a lógica dos comandos de forma limpa.
void Ext2Shell::cmd_attr(const std::string& name) {
    std::cout << "cmd_attr ainda não implementado." << std::endl;
}
void Ext2Shell::cmd_cat(const std::string& name) {
    // Verifica se o arquivo existe
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: File '" << name << "' not found." << std::endl;
        return;
    }

    ext2_inode fileInode;
    readInode(inodeNum, &fileInode);

    // Verifica se é um arquivo regular
    if (!S_ISREG(fileInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is not a regular file." << std::endl;
        return;
    }

    // Lê os blocos do arquivo e imprime o conteúdo
    std::vector<char> buffer(blockSize);
    unsigned int bytesRead = 0;
    const unsigned int fileSize = fileInode.i_size; // Tamanho do arquivo

    // Lê os 12 blocos diretos do inode do arquivo
    for (int i = 0; i < 12 && bytesRead < fileSize; i++) {
        // Se o ponteiro do bloco for 0, não há mais blocos para ler
        if (fileInode.i_block[i] == 0) break;

        // Lê o bloco do arquivo de origem pro buffer
        readBlock(fileInode.i_block[i], buffer.data());

        unsigned int bytesToWrite = std::min((unsigned int)blockSize,fileSize - bytesRead); // Calcula quantos bytes escrever neste bloco

        // Imprime o conteúdo do bloco lido
        std::cout.write(buffer.data(), bytesToWrite);
        bytesRead += bytesToWrite;
    }

    // Bloco indireto simples (12º bloco)
    if (bytesRead < fileSize && fileInode.i_block[12] != 0) {
        std::vector<unsigned int> indirectBlockPointers(blockSize / sizeof(unsigned int));
        readBlock(fileInode.i_block[12], indirectBlockPointers.data());

        // Percorre os ponteiros do bloco indireto
        for (unsigned int dataBlockNum : indirectBlockPointers) {
            if (dataBlockNum == 0) break; // Se o ponteiro for 0, não há mais blocos

            readBlock(dataBlockNum, buffer.data());
            unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesRead);
            std::cout.write(buffer.data(), bytesToWrite);
            bytesRead += bytesToWrite;
        }
    }

    // Bloco indireto duplo (13º bloco)
    if (bytesRead < fileSize && fileInode.i_block[13] !=0) {
        std::vector<unsigned int> doubleIndirectBlockPointers(blockSize / sizeof(unsigned int));
        readBlock(fileInode.i_block[13], doubleIndirectBlockPointers.data());

        // Percorre os ponteiros do bloco indireto duplo
        for (unsigned int indirectBlockNum : doubleIndirectBlockPointers) {
            if (indirectBlockNum == 0) break; // Se o ponteiro for 0, não há mais blocos

            std::vector<unsigned int> indirectDataBlockPointers(blockSize / sizeof(unsigned int));
            readBlock(indirectBlockNum, indirectDataBlockPointers.data());

            // Lê cada bloco apontado pelo bloco indireto duplo
            for (unsigned int dataBlockNum : indirectDataBlockPointers) {
                if (dataBlockNum == 0) break; // Se o ponteiro for 0, não há mais blocos

                readBlock(dataBlockNum, buffer.data());
                unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesRead);
                std::cout.write(buffer.data(), bytesToWrite);
                bytesRead += bytesToWrite;
            }
        }
    }

    // Verifica se leu todo o arquivo
    if (bytesRead < fileSize) {
        std::cerr << "Error: Failed to read entire file '" << name << "'." << std::endl;
    }

    // Adiciona uma linha vazia ao final da saída
    std::cout << std::endl;
}

void Ext2Shell::cmd_touch(const std::string& name) {
    // Validar comprimento do nome
    if (name.length() >= EXT2_NAME_LEN) {
        std::cerr << "Error: Name too long (max " << EXT2_NAME_LEN - 1 << " characters)." << std::endl;        return;
    }

    // Verifica se o arquivo já existe no diretório atual
    if (getInodeByName(name) != 0) {
        std::cerr << "Error: File '" << name << "' already exists." << std::endl;
        return;
    }

    // Tenta alocar um novo inode
    int newInodeNum = allocateInode();
    if (newInodeNum < 0) {
        std::cerr << "Error: No free inodes available." << std::endl;
        return;
    }

    // Inicializa o novo inode com valores padrão para arquivo regular
    ext2_inode newInode = {};
    newInode.i_mode = EXT2_S_IFREG | 0644;
    newInode.i_uid = 0;
    newInode.i_gid = 0;
    newInode.i_size = 0;
    newInode.i_links_count = 1;
    newInode.i_blocks = 0;
    newInode.i_atime = newInode.i_ctime = newInode.i_mtime = (uint32_t)time(nullptr);
    memset(newInode.i_block, 0, sizeof(newInode.i_block));

    // Escreve o novo inode no disco
    writeInode(newInodeNum, &newInode);

    // Adiciona a entrada do arquivo no diretório atual
    if (addDirectoryEntry(currentInodeNum, newInodeNum, name, EXT2_FT_REG_FILE) < 0) {
        std::cerr << "Error: Failed to add directory entry." << std::endl;
        freeInode(newInodeNum);
        return;
    }

    std::cout << "File '" << name << "' created successfully." << std::endl;
}

void Ext2Shell::cmd_mkdir(const std::string& name) {
    // Verifica se já existe arquivo ou diretório com o mesmo nome
    if (getInodeByName(name) != 0) {
        std::cerr << "Error: File or directory '" << name << "' already exists." << std::endl;
        return;
    }

    // Aloca um inode para o novo diretório
    int inodeNum = allocateInode();
    if (inodeNum < 0) {
        std::cerr << "Error: No free inodes available." << std::endl;
        return;
    }

    // Aloca um bloco para armazenar as entradas '.' e '..'
    int blockNum = allocateBlock();
    if (blockNum < 0) {
        std::cerr << "Error: No free blocks available." << std::endl;
        freeInode(inodeNum); // Libera o inode já alocado
        return;
    }

    // Prepara o inode com as propriedades do diretório
    ext2_inode dirInode = {};
    dirInode.i_mode = EXT2_S_IFDIR | 0755;  // Diretório com permissão rwxr-xr-x
    dirInode.i_uid = 0;
    dirInode.i_gid = 0;
    dirInode.i_size = blockSize;             // Tamanho do diretório: 1 bloco
    dirInode.i_links_count = 2;              // '.' e referência do pai
    dirInode.i_blocks = blockSize / 512;     // número de setores de 512 bytes
    dirInode.i_atime = dirInode.i_ctime = dirInode.i_mtime = (uint32_t)time(nullptr);
    memset(dirInode.i_block, 0, sizeof(dirInode.i_block));
    dirInode.i_block[0] = blockNum;          // Aponta para o bloco alocado

    // Grava o inode no disco
    writeInode(inodeNum, &dirInode);

    // Cria as entradas especiais '.' e '..' no bloco do diretório
    std::vector<char> blockData(blockSize, 0);
    ext2_dir_entry_2* dotEntry = reinterpret_cast<ext2_dir_entry_2*>(blockData.data());

    // Entrada '.'
    dotEntry->inode = inodeNum;
    dotEntry->rec_len = 12;
    dotEntry->name_len = 1;
    dotEntry->file_type = EXT2_FT_DIR;
    dotEntry->name[0] = '.';

    // Entrada '..' logo após '.'
    ext2_dir_entry_2* dotDotEntry = reinterpret_cast<ext2_dir_entry_2*>(blockData.data() + 12);
    dotDotEntry->inode = currentInodeNum;
    dotDotEntry->rec_len = blockSize - 12;
    dotDotEntry->name_len = 2;
    dotDotEntry->file_type = EXT2_FT_DIR;
    dotDotEntry->name[0] = '.';
    dotDotEntry->name[1] = '.';

    // Escreve o bloco do diretório no disco
    writeBlock(blockNum, blockData.data());

    // Adiciona a entrada do novo diretório no diretório atual
    if (addDirectoryEntry(currentInodeNum, inodeNum, name, EXT2_FT_DIR) < 0) {
        std::cerr << "Error: Failed to add directory entry." << std::endl;
        freeBlock(blockNum);   // Libera bloco e inode em caso de falha
        freeInode(inodeNum);
        return;
    }

    // Atualiza o link count do diretório atual (referência ao novo diretório)
    currentInode.i_links_count++;
    writeInode(currentInodeNum, &currentInode);

    std::cout << "Directory '" << name << "' created successfully." << std::endl;
}

void Ext2Shell::cmd_rm(const std::string& name) {
    // Verifica se existe um arquivo ou diretório com o nome
    if (getInodeByName(name) == 0) {
        std::cerr << "Error: File or directory named '" << name << "' does not exist." << std::endl;
        return;
    }

    // Verifica se é um arquivo ou um diretório
    ext2_inode targetInode;
    readInode(getInodeByName(name), &targetInode);

    // Se for um diretório, não pode remover com rm
    if (S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is a directory. 'rmdir' should be used instead." << std::endl;
        return;
    }

    // Remove a entrada do diretório pai
    ext2_dir_entry_2* prev_entry = nullptr;
    bool entryRemoved = false;
    // Percorre as entradas do diretório pai (currentInodeNum)
    for (int i = 0; i < 12 && !entryRemoved; i++) {
        if (currentInode.i_block[i] == 0) continue; // pula blocos vazios

        // Lê o bloco do diretório
        std::vector<char> blockData(blockSize);
        readBlock(currentInode.i_block[i], blockData.data());

        unsigned int offset = 0;
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];
            if (entry->inode == 0) { // Inode = 0, a entrada está vazia ou ja foi apagada
                offset += entry->rec_len; // Avança para a próxima entrada
                continue;
            }

            std::string entryName(entry->name, entry->name_len);

            // Verifica se é a entrada que queremos remover
            if (entryName == name) {
                if (prev_entry != nullptr) {
                    // Se não for a primeira entrada, ajusta o tamanho da entrada anterior
                    prev_entry->rec_len += entry->rec_len; // Une com a entrada anterior
                } else {
                    // Se for a primeira entrada, apenas zera o inode
                    entry->rec_len = blockSize - offset; // Ajusta o tamanho da entrada para ocupar o resto do bloco
                }

                writeBlock(currentInode.i_block[i], blockData.data()); // Escreve o bloco atualizado no disco
                entryRemoved = true;
                break;
            }
            prev_entry = entry; // Atualiza o ponteiro para a entrada anterior
            offset += entry->rec_len; // Avança para a próxima entrada
        }
    }

    // Decrementa o contador de links do arquivo removido
    readInode(getInodeByName(name), &targetInode);
    targetInode.i_links_count--;

    writeInode(getInodeByName(name), &targetInode); // Escreve o inode atualizado no disco

    // Libera os recursos do arquivo removido se for necessário
    if (targetInode.i_links_count == 0) {
        std::cout << "Removing file '" << name << "' and freeing its resources." << std::endl;
        for (int i = 0; i < 12; i++) {
            if (targetInode.i_block[i] != 0) {
                freeBlock(targetInode.i_block[i]); // Libera o bloco do arquivo
            }
        }
        freeInode(getInodeByName(name)); // Libera o inode do arquivo
    }

    if (entryRemoved) {
        std::cout << "File '" << name << "' removed successfully." << std::endl;
    } else {
        std::cerr << "Error: File '" << name << "' not found in current directory." << std::endl;
    }
}

void Ext2Shell::cmd_rmdir(const std::string& name) {
    // Verifica se existe um arquivo ou diretório com o nome
    unsigned int targetInodeNum = getInodeByName(name);
    if (targetInodeNum == 0) {
        std::cerr << "Error: File or directory named '" << name << "' does not exist." << std::endl;
        return;
    }

    // Verifica se é um arquivo ou um diretório
    ext2_inode targetInode;
    readInode(getInodeByName(name), &targetInode);

    // Se não for um diretório, não pode remover
    if (!S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is not a directory." << std::endl;
        return;
    }

    // Verifica se o diretório não tem nenhum subdiretório
    if (targetInode.i_links_count > 2) {
        std::cerr << "Error: Directory '" << name << "' is not empty." << std::endl;
        return;
    }

    bool isTrulyEmpty = true; // Assume que o diretório está vazio
    // Varredura do bloco de dados do diretório alvo
    if (targetInode.i_block[0] != 0) {
        std::vector<char> blockData(blockSize);
        readBlock(targetInode.i_block[0], blockData.data());

        unsigned int offset = 0;
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];
            if (entry->rec_len == 0) break; // Se a entrada não tiver tamanho, sai do loop

            // Verifica se o inode é valido
            if (entry->inode != 0) {
                std::string entryName(entry->name, entry->name_len);
                // Se encontrar uma entrada diferente de '.' e '..', o diretório não está vazio
                if (entryName != "." && entryName != "..") {
                    isTrulyEmpty = false;
                    break; // Sai do loop, pois já sabemos que não está vazio
                }
            }
            offset += entry->rec_len; // Avança para a próxima entrada
        }
    }

    if (!isTrulyEmpty) {
        std::cerr << "Error: Directory '" << name << "' is not empty." << std::endl;
        return;
    }

    // Remove a entrada do diretório pai
    bool entryRemoved = false;
    // Percorre as entradas do diretório pai (currentInodeNum)
    for (int i = 0; i < 12 && !entryRemoved; i++) {
        if (currentInode.i_block[i] == 0) continue; // pula blocos vazios

        ext2_dir_entry_2* prev_entry = nullptr; // Ponteiro para a entrada anterior

        // Lê o bloco do diretório
        std::vector<char> blockData(blockSize);
        readBlock(currentInode.i_block[i], blockData.data());

        unsigned int offset = 0;
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];
            if (entry->rec_len == 0) break; // Se a entrada não tiver tamanho, sai do loop
            if (entry->inode == 0) { // Inode = 0, a entrada está vazia ou ja foi apagada
                offset += entry->rec_len; // Avança para a próxima entrada
                continue;
            }

            std::string entryName(entry->name, entry->name_len);

            // Verifica se é a entrada que queremos remover
            if (entryName == name) {
                if (prev_entry != nullptr) {
                    // Se não for a primeira entrada, ajusta o tamanho da entrada anterior
                    prev_entry->rec_len += entry->rec_len; // Une com a entrada anterior
                } else {
                    // Se for a primeira entrada, apenas zera o inode
                    entry->inode = 0; // Ajusta o inode para 0
                }

                writeBlock(currentInode.i_block[i], blockData.data()); // Escreve o bloco atualizado no disco
                entryRemoved = true;
                break;
            }
            prev_entry = entry; // Atualiza o ponteiro para a entrada anterior
            offset += entry->rec_len; // Avança para a próxima entrada
        }
    }

    // Se por algum motivo a entrada não foi removida.
    if (!entryRemoved) {
        std::cerr << "Error: Could not remove directory entry for '" << name << "'." << std::endl;
        return;
    }

    // Atualiza o Inode do diretório pai
    currentInode.i_links_count--;
    writeInode(currentInodeNum, &currentInode);

    // Libera os recursos do diretório removido
    if (targetInode.i_block[0] != 0) {
        freeBlock(targetInode.i_block[0]); // Libera o bloco do diretório
    }

    freeInode(targetInodeNum); // Libera o inode do diretório

    if (entryRemoved) {
        std::cout << "Directory '" << name << "' removed successfully." << std::endl;
    } else {
        std::cerr << "Error: Directory '" << name << "' not found in current directory." << std::endl;
    }
}

// Exemplo: cp file.txt /home/user/file_copy.txt (não utilizar aspas nem espaços no caminho do arquivo)
void Ext2Shell::cmd_cp(const std::string& source, const std::string& dest) {
    // Verifica se o arquivo de origem existe
    unsigned int sourceInodeNum = getInodeByName(source);
    if (sourceInodeNum == 0) {
        std::cerr << "Error: Source file '" << source << "' does not exist." << std::endl;
        return;
    }

    // Lê o inode do arquivo de origem
    ext2_inode sourceInode;
    readInode(sourceInodeNum, &sourceInode);

    // Verifica se é um arquivo regular
    if (!S_ISREG(sourceInode.i_mode)) {
        std::cerr << "Error: Source '" << source << "' is not a regular file." << std::endl;
        return;
    }

    // Abre o fluxo de saída para o arquivo de destino
    // Se o arquivo ja existe, vai ser sobrescrito
    std::ofstream outFile(dest, std::ios::out | std::ios::binary);
    if (!outFile.is_open()) { // Verifica se o arquivo de destino pode ser aberto (o arquivo foi criado com sucesso)
        std::cerr << "Error: Could not open destination file '" << dest << "' for writing." << std::endl;
        return;
    }

    // Buffer temporario para ler os blocos do arquivo
    std::vector<char> buffer(blockSize);
    unsigned int bytesCopied = 0;
    const unsigned int fileSize = sourceInode.i_size; // Tamanho do arquivo de origem

    // Percorre os 12 blocos diretos do inode do arquivo de origem
    for (int i = 0; i < 12 && bytesCopied < fileSize; i++) {
        // Se o ponteiro do bloco for 0, não há mais blocos para ler
        if (sourceInode.i_block[i] == 0) break;

        // Lê o bloco do arquivo de origem pro buffer
        readBlock(sourceInode.i_block[i], buffer.data());

        unsigned int bytesToWrite = std::min((unsigned int)blockSize,fileSize - bytesCopied); // Calcula quantos bytes escrever neste bloco

        outFile.write(buffer.data(), bytesToWrite); // Escreve o bloco no arquivo de destino

        // Atualiza o contador de bytes copiados
        bytesCopied += bytesToWrite;
    }

    // Processa os blocos indiretos, se necessário (i_block[12] e i_block[13])
    if (bytesCopied < fileSize && sourceInode.i_block[12] != 0) {
        // Lê o bloco indireto
        // O bloco indireto contém ponteiros para blocos de dados
        std::vector<unsigned int> indirectBlockpointers(blockSize / sizeof(unsigned int));
        readBlock(sourceInode.i_block[12], (char*)indirectBlockpointers.data());

        //  Percorre os ponteiros do bloco indireto
        for (unsigned int dataBlockNum : indirectBlockpointers) {
            if (bytesCopied >= fileSize || dataBlockNum == 0) break; // Se já copiou todo o arquivo, sai do loop

            // Lê o bloco de dados apontado pelo ponteiro indireto
            readBlock(dataBlockNum, buffer.data());

            unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesCopied); // Calcula quantos bytes escrever neste bloco
            outFile.write(buffer.data(), bytesToWrite); // Escreve o bloco no arquivo de destino
            bytesCopied += bytesToWrite; // Atualiza o contador de bytes copiados
        }
    }

    if (bytesCopied < fileSize && sourceInode.i_block[13] != 0) {
        // Lê o bloco indireto duplo
        std::vector<unsigned int> doubleIndirectBlockPointers(blockSize / sizeof(unsigned int));
        readBlock(sourceInode.i_block[13], (char*)doubleIndirectBlockPointers.data());

        // Percorre cada ponteiro para um bloco indireto
        for (unsigned int singleIndirectBlockNum : doubleIndirectBlockPointers) {
            if (bytesCopied >= fileSize || singleIndirectBlockNum == 0) break; // Se já copiou todo o arquivo, sai do loop

            // Buffer para ler os blocos indiretos simples
            std::vector<unsigned int> singleIndirectBlockPointers(blockSize / sizeof(unsigned int));
            readBlock(singleIndirectBlockNum, (char*)singleIndirectBlockPointers.data());

            // Percorre os ponteiros do bloco de dados indireto simples
            for (unsigned int dataBlockNum : singleIndirectBlockPointers) {
                if (bytesCopied >= fileSize || dataBlockNum == 0) break; // Se já copiou todo o arquivo, sai do loop

                // Lê o bloco de dados apontado pelo ponteiro indireto simples
                readBlock(dataBlockNum, buffer.data());

                unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesCopied); // Calcula quantos bytes escrever neste bloco
                outFile.write(buffer.data(), bytesToWrite); // Escreve o bloco no arquivo de destino
                bytesCopied += bytesToWrite; // Atualiza o contador de bytes copiados
            }
        }
    }

    outFile.close(); // Fecha o arquivo de destino

    std::cout << "File '" << source << "' copied to '" << dest << "' successfully." << std::endl;
}

void Ext2Shell::cmd_rename(const std::string& oldName, const std::string& newName) {
    // Valida comprimento
    if (newName.length() >= EXT2_NAME_LEN) {
        std::cerr << "Error: Name too long (max " << EXT2_NAME_LEN - 1 << " characters)." << std::endl;        return;
    }

    // Verifica se oldName existe
    unsigned int oldIno = getInodeByName(oldName);
    if (oldIno == 0) {
         std::cerr << "Error: '" << oldName << "' not found." << std::endl;
        return;
    }

    // Verifica se newName já existe
    if (getInodeByName(newName) != 0) {
        std::cerr << "Error: '" << newName << "' already exists." << std::endl;
        return;
    }

    // 4) Determina o file_type (arquivo ou diretório)
    ext2_inode targetInode;
    readInode(oldIno, &targetInode);
    unsigned char fileType = S_ISDIR(targetInode.i_mode) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    // Cria a nova entrada (link)
    if (addDirectoryEntry(currentInodeNum, oldIno, newName, fileType) < 0) {
        std::cerr << "Error: Insufficient space to create '" << newName << "'." << std::endl;
        return;
    }

    // Remove a entrada antiga
    removeDirectoryEntry(currentInodeNum, oldName);

    // Atualiza o link count no inode (decrementa uma referência)
    targetInode.i_links_count--;
    writeInode(oldIno, &targetInode);

    std::cout << "Renamed '" << oldName << "' to '" << newName << "' successfully." << std::endl;
}