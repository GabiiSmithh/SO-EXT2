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
Ext2Shell::Ext2Shell(const std::string& imagePath) : fd(open(imagePath.c_str(), O_RDWR)), imagePath(imagePath) {
    if (fd < 0) {
        // Usamos this->imagePath para ser explícito que estamos usando o membro da classe.
        throw std::runtime_error("Error: Could not open image file '" + this->imagePath + "'.");
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

// Calcula o deslocamento do bloco no disco
inline unsigned int block_offset(unsigned int block, unsigned int blockSize) {
    return BASE_OFFSET + (block - 1) * blockSize;
}

// Lê blocos inteiros do disco
void Ext2Shell::readBlock(unsigned int block, void* buffer) {
    lseek(fd, block_offset(block, blockSize), SEEK_SET);
    read(fd, buffer, blockSize);
}

// Escreve blocos inteiros no disco
void Ext2Shell::writeBlock(unsigned int block, const void* buffer) {
    lseek(fd, block_offset(block, blockSize), SEEK_SET);
    write(fd, buffer, blockSize);
}

// Lê descritores de grupo
void Ext2Shell::readGroupDesc(unsigned int groupNum, ext2_group_desc* group) {
    lseek(fd, BASE_OFFSET + blockSize + groupNum * sizeof(ext2_group_desc), SEEK_SET);
    read(fd, group, sizeof(ext2_group_desc));
}

// Escreve descritores de grupo
void Ext2Shell::writeGroupDesc(unsigned int groupNum, const ext2_group_desc* group) {
    lseek(fd, BASE_OFFSET + blockSize + groupNum * sizeof(ext2_group_desc), SEEK_SET);
    write(fd, group, sizeof(struct ext2_group_desc));
}

// Lê um inode específico do disco
void Ext2Shell::readInode(unsigned int inodeNum, ext2_inode* inode) {
    unsigned int group = (inodeNum - 1) / super.s_inodes_per_group;
    ext2_group_desc groupDesc;
    readGroupDesc(group, &groupDesc);

    unsigned int index = (inodeNum - 1) % super.s_inodes_per_group;
    unsigned int offset = groupDesc.bg_inode_table * blockSize + index * sizeof(ext2_inode);
    lseek(fd, BASE_OFFSET - 1024 + offset, SEEK_SET);
    read(fd, inode, sizeof(ext2_inode));
}

// Escreve um inode específico no disco
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

// Executa o loop principal do shell
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

// Gera o prompt baseado no caminho atual
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

// Processa um comando lido do usuário
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

// Exibe informações do sistema de arquivos
void Ext2Shell::cmd_info() {
    std::cout << "Volume name.....: " << super.s_volume_name << std::endl;
    std::cout << "Image size......: " << super.s_blocks_count * blockSize << " KiB" << std::endl;
    std::cout << "Free space......: " << super.s_free_blocks_count * blockSize << " KiB" << std::endl;
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

// Lê todos os blocos de dados de um inode e chama o callback para cada bloco
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

// Procura um inode livre em todos os grupos
int Ext2Shell::findFreeInode() {
    unsigned int numGroups = (super.s_inodes_count + super.s_inodes_per_group - 1) / super.s_inodes_per_group;
    // Percorre todos os grupos de inodes
    for (unsigned int group = 0; group < numGroups; ++group) {
        ext2_group_desc groupDesc;
        readGroupDesc(group, &groupDesc);
        // Se o grupo não tem inodes livres, pula para o próximo
        if (groupDesc.bg_free_inodes_count > 0) {
            unsigned char bitmap[blockSize];
            readBlock(groupDesc.bg_inode_bitmap, bitmap);
            // Percorre o bitmap do grupo
            for (unsigned int i = 0; i < super.s_inodes_per_group; ++i) {
                // Se o bit correspondente ao inode não está marcado, é um inode livre
                if (!isBitSet(bitmap, i)) {
                    return group * super.s_inodes_per_group + i + 1;
                }
            }
        }
    }
    return -1; // Nenhum inode livre em nenhum grupo
}

// Procura um bloco livre em todos os grupos
int Ext2Shell::findFreeBlock() {
    unsigned int numGroups = (super.s_blocks_count + super.s_blocks_per_group - 1) / super.s_blocks_per_group;
    // Percorre todos os grupos de blocos
    for (unsigned int group = 0; group < numGroups; ++group) {
        ext2_group_desc groupDesc;
        readGroupDesc(group, &groupDesc);
        // Se o grupo não tem blocos livres, pula para o próximo
        if (groupDesc.bg_free_blocks_count > 0) {
            unsigned char bitmap[blockSize];
            readBlock(groupDesc.bg_block_bitmap, bitmap);
            // Percorre o bitmap do grupo
            for (unsigned int i = 0; i < super.s_blocks_per_group; ++i) {
                // Se o bit correspondente ao bloco não está marcado, é um bloco livre
                if (!isBitSet(bitmap, i)) {
                    return group * super.s_blocks_per_group + i + 1;
                }
            }
        }
    }
    return -1; // Nenhum bloco livre em nenhum grupo
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

// Libera um inode no grupo correto
void Ext2Shell::freeInode(unsigned int inodeNum) {
    if (inodeNum == 0) return;
    // Calcula em qual grupo este inode realmente está
    unsigned int group = (inodeNum - 1) / super.s_inodes_per_group;
    // Lê o descritor do grupo correto
    ext2_group_desc groupDesc;
    readGroupDesc(group, &groupDesc);
    // Lê e modifica o bitmap do grupo correto
    unsigned char bitmap[blockSize];
    readBlock(groupDesc.bg_inode_bitmap, bitmap);
    int bit = (inodeNum - 1) % super.s_inodes_per_group;
    bitmap[bit / 8] &= ~(1 << (bit % 8)); // Limpa o bit
    writeBlock(groupDesc.bg_inode_bitmap, bitmap);
    // Atualiza os contadores do superbloco e do grupo correto
    super.s_free_inodes_count++;
    groupDesc.bg_free_inodes_count++;
    // Se o inode era um diretório, também ajustamos o contador de diretórios
    ext2_inode inode;
    readInode(inodeNum, &inode);
    if(S_ISDIR(inode.i_mode)) {
        groupDesc.bg_used_dirs_count--;
    }
    // Salva os metadados atualizados.
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(group, &groupDesc);
}

// Libera um bloco no grupo correto
void Ext2Shell::freeBlock(unsigned int blockNum) {
    if (blockNum == 0) return;

    // Calcula em qual grupo este bloco realmente está
    unsigned int group = (blockNum - 1) / super.s_blocks_per_group; 
    // Lê o descritor do grupo correto
    ext2_group_desc groupDesc;
    readGroupDesc(group, &groupDesc);
    // Lê e modifica o bitmap do grupo correto.
    unsigned char bitmap[blockSize];
    readBlock(groupDesc.bg_block_bitmap, bitmap);
    int bit = (blockNum - 1) % super.s_blocks_per_group;
    bitmap[bit / 8] &= ~(1 << (bit % 8)); // Limpa o bit
    writeBlock(groupDesc.bg_block_bitmap, bitmap);
    // Atualiza os contadores do superbloco e do grupo correto.
    super.s_free_blocks_count++;
    groupDesc.bg_free_blocks_count++;
    // Salva os metadados atualizados.
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(group, &groupDesc);
}

std::string formatPermissions(unsigned short mode) {
    std::string p;
    // Determine file type
    if (S_ISDIR(mode)) p += 'd';
    else if (S_ISREG(mode)) p += 'f';
    else if (S_ISLNK(mode)) p += 'l';
    else p += '?';

    // User permissions
    p += (mode & S_IRUSR) ? 'r' : '-';
    p += (mode & S_IWUSR) ? 'w' : '-';
    p += (mode & S_IXUSR) ? 'x' : '-';
    // Group permissions
    p += (mode & S_IRGRP) ? 'r' : '-';
    p += (mode & S_IWGRP) ? 'w' : '-';
    p += (mode & S_IXGRP) ? 'x' : '-';
    // Other permissions
    p += (mode & S_IROTH) ? 'r' : '-';
    p += (mode & S_IWOTH) ? 'w' : '-';
    p += (mode & S_IXOTH) ? 'x' : '-';
    
    return p;
}

std::string formatSize(unsigned int size) {
    std::stringstream ss;
    double s = static_cast<double>(size);
    ss << std::fixed << std::setprecision(1);

    if (s < 1024) {
        ss << std::setprecision(0) << s << " B";
    } else if (s < 1024 * 1024) {
        ss << (s / 1024.0) << " KiB";
    } else if (s < 1024 * 1024 * 1024) {
        ss << (s / (1024.0 * 1024.0)) << " MiB";
    } else {
        ss << (s / (1024.0 * 1024.0 * 1024.0)) << " GiB";
    }
    return ss.str();
}


std::string formatTime(uint32_t timestamp) {
    time_t raw_time = timestamp;
    struct tm* timeinfo = localtime(&raw_time);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M", timeinfo);
    return std::string(buffer);
}


// Lista os arquivos e diretórios no diretório atual
void Ext2Shell::cmd_ls() {
    forEachDirEntry(currentInodeNum, [](ext2_dir_entry_2* entry) {
        std::string name(entry->name, entry->name_len);
        std::cout << name << std::endl;
        std::cout << "inode: " << entry->inode << std::endl;
        std::cout << "record length: " << entry->rec_len << std::endl;
        std::cout << "name lenght: " << static_cast<int>(entry->name_len) << std::endl;
        std::cout << "file type: " << static_cast<int>(entry->file_type) << std::endl;
        std::cout << std::endl;
        return true;
    });
}

// Muda o diretório atual para o especificado
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

// Exibe o caminho atual
void Ext2Shell::cmd_pwd() {
    // Inicia a string do caminho sempre com a barra da raiz.
    std::string pathStr = "/";

    // Percorre o vetor que armazena as partes do caminho (ex: "banana", "subpasta").
    for (const auto& part : currentPath) {
        // Adiciona cada parte do caminho seguida por uma barra.
        pathStr += part + "/";
    }

    // Imprime a string do caminho final.
    std::cout << pathStr << std::endl;
}

// --- Implementações dos Comandos de Manipulação de Arquivos e Diretórios ---

// Exibe os atributos de um arquivo ou diretório
void Ext2Shell::cmd_attr(const std::string& name) {
    // Encontra o inode pelo nome no diretório atual
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: File or directory '" << name << "' not found." << std::endl;
        return;
    }

    // Ler os dados do inode
    ext2_inode targetInode;
    readInode(inodeNum, &targetInode);

    // Formatar as informações extraidas
    std::string perms_str = formatPermissions(targetInode.i_mode);
    std::string size_str = formatSize(targetInode.i_size);
    std::string mtime_str = formatTime(targetInode.i_mtime);

    // Printar o header e as informações extraidas
    std::cout << " permissões  uid  gid      tamanho      modificado em" << std::endl;
    std::cout << std::left 
              << std::setw(12) << perms_str
              << std::setw(5) << targetInode.i_uid
              << std::setw(5) << targetInode.i_gid
              << std::setw(15) << size_str
              << mtime_str << std::endl;
}

// Exibe o conteúdo de um arquivo
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

// Cria um novo arquivo vazio
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

// Cria um novo diretório
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

    // Incrementa o contador de links do diretório pai
    currentGroupDesc.bg_used_dirs_count++;
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    std::cout << "Directory '" << name << "' created successfully." << std::endl;
}

// Remove um arquivo
void Ext2Shell::cmd_rm(const std::string& name) {
    // Encontra o inode do arquivo pelo nome
    unsigned int targetInodeNum = getInodeByName(name);
    if (targetInodeNum == 0) {
        std::cerr << "Error: File '" << name << "' not found." << std::endl;
        return;
    }

    // Verifica se é um arquivo regular
    ext2_inode targetInode;
    readInode(targetInodeNum, &targetInode);
    if (S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is a directory. Use 'rmdir' instead." << std::endl;
        return;
    }

    // Remove a entrada do diretório pai
    bool entryRemoved = false;
    for (int i = 0; i < 12 && !entryRemoved; i++) {
        if (currentInode.i_block[i] == 0) continue;

        // Lê o bloco de dados do diretório atual
        std::vector<char> blockData(blockSize);
        readBlock(currentInode.i_block[i], blockData.data());
        char* blockStart = blockData.data();
        
        // Ponteiros para as entradas do diretório
        ext2_dir_entry_2* entry_to_delete = nullptr;
        ext2_dir_entry_2* prev_to_delete = nullptr; // O anterior ao que vamos deletar
        ext2_dir_entry_2* current_entry = (ext2_dir_entry_2*)blockStart;
        unsigned int offset = 0;

        // Varre o bloco para encontrar a entrada a ser deletada e sua anterior
        while (offset < blockSize) {
            if (current_entry->rec_len == 0) break;
            if (current_entry->inode == targetInodeNum && std::string(current_entry->name, current_entry->name_len) == name) {
                entry_to_delete = current_entry;
                break;
            }
            if(current_entry->inode != 0) prev_to_delete = current_entry;
            offset += current_entry->rec_len;
            current_entry = (ext2_dir_entry_2*)(blockStart + offset);
        }

        // Se encontrou a entrada
        if (entry_to_delete) {
            // Verifica se a entrada a deletar é a última na lista
            unsigned int deleteOffset = (char*)entry_to_delete - blockStart;
            bool isLastEntry = (deleteOffset + entry_to_delete->rec_len >= blockSize);

            // CASO DE BORDA: Removendo o último item
            if (isLastEntry) {
                if (prev_to_delete) {
                    prev_to_delete->rec_len += entry_to_delete->rec_len;
                } else { // Só havia um item no diretório
                    entry_to_delete->inode = 0;
                }
            } 
            // CASO GERAL: Removendo do meio
            else {
                char* deletePtr = reinterpret_cast<char*>(entry_to_delete);
                unsigned short deleteLen = entry_to_delete->rec_len;
                char* nextPtr = deletePtr + deleteLen;
                unsigned int moveLen = (blockStart + blockSize) - nextPtr;

                // Move as entradas seguintes para preencher o espaço
                memmove(deletePtr, nextPtr, moveLen);
                
                // Encontra o novo último item e ajusta seu rec_len
                ext2_dir_entry_2* new_last_entry = nullptr;
                offset = 0;
                while (offset < (blockSize - deleteLen)) {
                    ext2_dir_entry_2* current = (ext2_dir_entry_2*)(blockStart + offset);
                    if (current->rec_len == 0) break;
                    new_last_entry = current;
                    offset += current->rec_len;
                }
                if (new_last_entry) {
                    offset = (char*)new_last_entry - blockStart;
                    new_last_entry->rec_len = blockSize - offset;
                }
            }

            writeBlock(currentInode.i_block[i], blockData.data());
            entryRemoved = true;
        }
    }

    if (!entryRemoved) { return; }

    // Libera o inode do arquivo
    targetInode.i_links_count--;
    writeInode(targetInodeNum, &targetInode);
    if (targetInode.i_links_count == 0) {
        // Define o tempo de deleção antes de começar a apagar
        targetInode.i_dtime = time(nullptr);
        writeInode(targetInodeNum, &targetInode);

        // Libera os 12 blocos de dados diretos
        for (int j = 0; j < 12; j++) {
            if (targetInode.i_block[j] != 0) {
                freeBlock(targetInode.i_block[j]);
            }
        }

        // Libera blocos do ponteiro indireto simples (i_block[12])
        if (targetInode.i_block[12] != 0) {
            std::vector<unsigned int> indirectBlock(blockSize / sizeof(unsigned int));
            readBlock(targetInode.i_block[12], indirectBlock.data());

            // Libera os blocos apontados pelo bloco de ponteiros indiretos simples
            for (unsigned int dataBlockNum : indirectBlock) {
                if (dataBlockNum != 0) freeBlock(dataBlockNum);
            }

            // Libera o próprio bloco de ponteiros indiretos
            freeBlock(targetInode.i_block[12]);
        }

        // Libera blocos do ponteiro indireto duplo (i_block[13])
        if (targetInode.i_block[13] != 0) {
            std::vector<unsigned int> doublyIndirectBlock(blockSize / sizeof(unsigned int));
            readBlock(targetInode.i_block[13], doublyIndirectBlock.data());

            // Percorre cada bloco de indireção simples apontado pelo bloco de indireção dupla
            for (unsigned int singlyIndirectBlockNum : doublyIndirectBlock) {
                if (singlyIndirectBlockNum == 0) continue;
                
                std::vector<unsigned int> singlyIndirectBlock(blockSize / sizeof(unsigned int));
                readBlock(singlyIndirectBlockNum, singlyIndirectBlock.data());

                // Libera os blocos apontados pelo bloco de indireção simples
                for (unsigned int dataBlockNum : singlyIndirectBlock) {
                    if (dataBlockNum != 0) freeBlock(dataBlockNum);
                }

                // Libera o bloco de indireção simples
                freeBlock(singlyIndirectBlockNum);
            }

            // Libera o próprio bloco de ponteiros indiretos duplos
            freeBlock(targetInode.i_block[13]);
        }
        freeInode(targetInodeNum);
    }

    std::cout << "File '" << name << "' removed successfully." << std::endl;
}

// Remove um diretório vazio
void Ext2Shell::cmd_rmdir(const std::string& name) {
    // Encontra o inode do diretório pelo nome
    unsigned int targetInodeNum = getInodeByName(name);
    if (targetInodeNum == 0) {
        std::cerr << "Error: Directory '" << name << "' not found." << std::endl;
        return;
    }

    // Verifica se é um diretório e se está vazio
    ext2_inode targetInode;
    readInode(targetInodeNum, &targetInode);

    if (!S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is not a directory." << std::endl;
        return;
    }

    if (targetInode.i_links_count > 2) {
        std::cerr << "Error: Directory '" << name << "' is not empty (contains subdirectories)." << std::endl;
        return;
    }

    // Verifica se está vazio (verificação mais rigorosa)
    bool isTrulyEmpty = true;
    if (targetInode.i_block[0] != 0) {
        std::vector<char> dirBlockData(blockSize);
        readBlock(targetInode.i_block[0], &dirBlockData[0]);
        unsigned int temp_offset = 0;
        while (temp_offset < blockSize) {
            ext2_dir_entry_2* e = (ext2_dir_entry_2*)&dirBlockData[temp_offset];
            if (e->rec_len == 0) break;
            if (e->inode != 0 && std::string(e->name, e->name_len) != "." && std::string(e->name, e->name_len) != "..") {
                isTrulyEmpty = false;
                break;
            }
            temp_offset += e->rec_len;
        }
    }
    if (!isTrulyEmpty) {
        std::cerr << "Error: Directory '" << name << "' is not empty (contains files)." << std::endl;
        return;
    }

    // Remove a entrada do diretório pai
    bool entryRemoved = false;
    for (int i = 0; i < 12 && !entryRemoved; i++) {
        if (currentInode.i_block[i] == 0) continue;

        std::vector<char> parentBlockData(blockSize);
        readBlock(currentInode.i_block[i], &parentBlockData[0]);
        char* blockStart = &parentBlockData[0];
        
        ext2_dir_entry_2* entry_to_delete = nullptr;
        ext2_dir_entry_2* last_entry = nullptr;
        unsigned int offset = 0;

        // Varre o bloco para encontrar a entrada a ser deletada e sua anterior
        while (offset < blockSize) {
            ext2_dir_entry_2* current_entry = (ext2_dir_entry_2*)(blockStart + offset);
            if (current_entry->rec_len == 0) break;
            if (current_entry->inode != 0) {
                last_entry = current_entry;
                if (std::string(current_entry->name, current_entry->name_len) == name) {
                    entry_to_delete = current_entry;
                }
            }
            offset += current_entry->rec_len;
        }

        // Se encontrou a entrada
        if (entry_to_delete) {
            // Verifica se a entrada a deletar é a última na lista
            unsigned int deleteOffset = (char*)entry_to_delete - blockStart;
            bool isLastEntry = (deleteOffset + entry_to_delete->rec_len >= blockSize);

            // CASO DE BORDA: Removendo o último item
            if (isLastEntry) {
                ext2_dir_entry_2* prev_to_last = nullptr;
                offset = 0;
                while(offset < blockSize){
                    ext2_dir_entry_2* current = (ext2_dir_entry_2*)(blockStart + offset);
                    if(current->rec_len == 0) break;
                    if(current == last_entry) break;
                    if(current->inode != 0) prev_to_last = current;
                    offset += current->rec_len;
                }
                if(prev_to_last) {
                     prev_to_last->rec_len += last_entry->rec_len;
                } else { // Só havia um item no diretório
                     last_entry->inode = 0;
                }
            } 
            // CASO GERAL: Removendo do meio
            else {
                char* deletePtr = reinterpret_cast<char*>(entry_to_delete);
                unsigned short deleteLen = entry_to_delete->rec_len;
                char* nextPtr = deletePtr + deleteLen;
                unsigned int moveLen = (blockStart + blockSize) - nextPtr;

                // Move as entradas seguintes para preencher o espaço
                memmove(deletePtr, nextPtr, moveLen);
                
                // Encontra o novo último item e ajusta seu rec_len
                ext2_dir_entry_2* new_last_entry = nullptr;
                offset = 0;
                while (offset < (blockSize - deleteLen)) {
                    ext2_dir_entry_2* current = (ext2_dir_entry_2*)(blockStart + offset);
                    if (current->rec_len == 0) break;
                    new_last_entry = current;
                    offset += current->rec_len;
                }
                if (new_last_entry) {
                    offset = (char*)new_last_entry - blockStart;
                    new_last_entry->rec_len = blockSize - offset;
                }
            }
            writeBlock(currentInode.i_block[i], parentBlockData.data());
            entryRemoved = true;
        }
    }

    if (!entryRemoved) {
        std::cerr << "Error: Could not remove directory entry for '" << name << "'." << std::endl;
        return;
    }

    // Atualiza o inode do diretório atual
    currentInode.i_links_count--;
    writeInode(currentInodeNum, &currentInode);

    // Atualiza o grupo do diretório atual
    currentGroupDesc.bg_used_dirs_count--;
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    // Atualiza o inode do diretório a ser deletado
    targetInode.i_links_count = 0;
    targetInode.i_dtime = time(nullptr);
    writeInode(targetInodeNum, &targetInode);

    // Libera o bloco de dados do diretório
    if (targetInode.i_block[0] != 0) {
        freeBlock(targetInode.i_block[0]);
    }

    // Libera o inode do diretório
    freeInode(targetInodeNum);

    std::cout << "Directory '" << name << "' removed successfully." << std::endl;
}

// Copia um arquivo do sistema de arquivos para o sistema local
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

// Renomeia um arquivo ou diretório
void Ext2Shell::cmd_rename(const std::string& oldName, const std::string& newName) {
    // Validações iniciais
    if (newName.length() >= EXT2_NAME_LEN) {
        std::cerr << "Error: New name is too long." << std::endl;
        return;
    }
    unsigned int inodeNum = getInodeByName(oldName);
    if (inodeNum == 0) {
        std::cerr << "Error: '" << oldName << "' not found." << std::endl;
        return;
    }
    if (getInodeByName(newName) != 0) {
        std::cerr << "Error: '" << newName << "' already exists." << std::endl;
        return;
    }

    // Lê o inode do arquivo ou diretório atual
    bool operationCompleted = false;
    for (int i = 0; i < 12 && !operationCompleted; i++) {
        if (currentInode.i_block[i] == 0) continue;

        std::vector<char> blockData(blockSize);
        readBlock(currentInode.i_block[i], blockData.data());
        
        unsigned int offset = 0;
        ext2_dir_entry_2* entry_to_rename = nullptr;
        // Encontra o ponteiro para a entrada que queremos renomear
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];
            if (entry->rec_len == 0) break;
            if (entry->inode == inodeNum && std::string(entry->name, entry->name_len) == oldName) {
                entry_to_rename = entry;
                break;
            }
            offset += entry->rec_len;
        }

        if (entry_to_rename) {
            // Calcula o espaço mínimo que a nova entrada precisa
            unsigned int neededLen = (8 + newName.length() + 3) & ~3;

            // Se o novo nome cabe na entrada atual, renomeia diretamente
            if (neededLen <= entry_to_rename->rec_len) {
                entry_to_rename->name_len = newName.length();
                memset(entry_to_rename->name, 0, oldName.length()); // Limpa o nome antigo
                strncpy(entry_to_rename->name, newName.c_str(), entry_to_rename->name_len);
                
                // Salva a alteração e termina
                writeBlock(currentInode.i_block[i], blockData.data());

            } else {
                // Se o novo nome não cabe, remove a entrada antiga e adiciona uma nova entrada com o novo nome
                ext2_dir_entry_2* prev = nullptr;
                unsigned int internal_offset = 0;
                while (internal_offset < blockSize) {
                    ext2_dir_entry_2* e = (ext2_dir_entry_2*)&blockData[internal_offset];
                    if (e->rec_len == 0) break;
                    if (e == entry_to_rename) {
                        if (prev) prev->rec_len += e->rec_len;
                        else e->inode = 0;
                        break;
                    }
                    if (e->inode != 0) prev = e;
                    internal_offset += e->rec_len;
                }
                
                // Escreve o bloco com a entrada já removida no disco
                writeBlock(currentInode.i_block[i], blockData.data());

                // Adiciona uma nova entrada com o novo nome
                ext2_inode targetInode;
                readInode(inodeNum, &targetInode);
                unsigned char fileType = S_ISDIR(targetInode.i_mode) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
                if (addDirectoryEntry(currentInodeNum, inodeNum, newName, fileType) < 0) {
                    std::cerr << "Error: Could not add new entry for '" << newName << "'. Filesystem may be inconsistent." << std::endl;
                    return;
                }
            }
            operationCompleted = true;
        }
    }

    if (operationCompleted) {
        std::cout << "Renamed '" << oldName << "' to '" << newName << "' successfully." << std::endl;
    } else {
        std::cerr << "Error: Could not find entry to rename." << std::endl;
    }
}