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
#include <ctime>

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
    lseek(fd, BASE_OFFSET + blockSize + groupNum * sizeof(struct ext2_group_desc), SEEK_SET);
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

    std::vector<char> buffer(blockSize);

    for (int i = 0; i < 12; i++) {
        if (inode.i_block[i] == 0) continue;
        readBlock(inode.i_block[i], buffer.data());
        callback(buffer);
    }
}

// Função para adicionar uma entrada de diretório no diretório pai
int Ext2Shell::addDirectoryEntry(unsigned int parentInodeNum, unsigned int childInodeNum, const std::string& name, unsigned char fileType) {
    ext2_inode parentInode;
    readInode(parentInodeNum, &parentInode);

    std::vector<char> blockData(blockSize);

    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == 0) continue;

        readBlock(parentInode.i_block[i], blockData.data());

        unsigned int offset = 0;
        while (offset < blockSize) {
            ext2_dir_entry_2* entry = (ext2_dir_entry_2*)&blockData[offset];
            unsigned int idealLen = 8 + ((entry->name_len + 3) & ~3);
            unsigned int leftover = entry->rec_len - idealLen;
            unsigned int neededLen = 8 + ((name.size() + 3) & ~3);

            if (leftover >= neededLen) {
                entry->rec_len = idealLen;
                ext2_dir_entry_2* newEntry = (ext2_dir_entry_2*)&blockData[offset + idealLen];
                newEntry->inode = childInodeNum;
                newEntry->rec_len = leftover;
                newEntry->name_len = name.size();
                newEntry->file_type = fileType;
                memcpy(newEntry->name, name.c_str(), name.size());
                writeBlock(parentInode.i_block[i], blockData.data());
                if (fileType == EXT2_FT_DIR) {
                    parentInode.i_links_count++;
                    writeInode(parentInodeNum, &parentInode);
                }
                return 0;
            }
            offset += entry->rec_len;
        }
    }
    return -1;
}

void Ext2Shell::removeDirectoryEntry(unsigned int parentInodeNum, const std::string& name) {
    ext2_inode parentInode;
    readInode(parentInodeNum, &parentInode);

    std::vector<char> blockData(blockSize);
    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == 0) continue;
        readBlock(parentInode.i_block[i], blockData.data());

        ext2_dir_entry_2* prev = nullptr;
        unsigned int offset = 0;
        while (offset < blockSize) {
            auto* e = reinterpret_cast<ext2_dir_entry_2*>(&blockData[offset]);
            if (e->inode == 0) break;

            std::string entryName(e->name, e->name_len);
            if (entryName == name) {
                if (prev) {
                    prev->rec_len += e->rec_len;
                } else {
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

bool Ext2Shell::isBitSet(unsigned char* bitmap, int bit) {
    int bytePos = bit / 8;
    int bitPos = bit % 8;
    return (bitmap[bytePos] & (1 << bitPos)) != 0;
}

int Ext2Shell::findFreeInode() {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap);

    for (unsigned int i = 0; i < super.s_inodes_per_group; i++) {
        if (!isBitSet(bitmap, i)) {
            return i + 1 + currentGroupNum * super.s_inodes_per_group;
        }
    }
    return -1;
}

int Ext2Shell::findFreeBlock() {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap);

    for (unsigned int i = 0; i < super.s_blocks_per_group; i++) {
        if (!isBitSet(bitmap, i)) {
            return i + 1 + currentGroupNum * super.s_blocks_per_group;
        }
    }
    return -1;
}

int Ext2Shell::allocateInode() {
    int inodeNum = findFreeInode();
    if (inodeNum < 0) return -1;

    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap);
    int bit = (inodeNum - 1) % super.s_inodes_per_group;
    bitmap[bit / 8] |= (1 << (bit % 8));
    writeBlock(currentGroupDesc.bg_inode_bitmap, bitmap);

    super.s_free_inodes_count--;
    currentGroupDesc.bg_free_inodes_count--;
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    return inodeNum;
}

int Ext2Shell::allocateBlock() {
    int blockNum = findFreeBlock();
    if (blockNum < 0) return -1;

    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap);
    int bit = (blockNum - 1) % super.s_blocks_per_group;
    bitmap[bit / 8] |= (1 << (bit % 8));
    writeBlock(currentGroupDesc.bg_block_bitmap, bitmap);

    super.s_free_blocks_count--;
    currentGroupDesc.bg_free_blocks_count--;
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(currentGroupNum, &currentGroupDesc);

    return blockNum;
}

void Ext2Shell::freeInode(unsigned int inodeNum) {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_inode_bitmap, bitmap);
    int bit = (inodeNum - 1) % super.s_inodes_per_group;
    bitmap[bit / 8] &= ~(1 << (bit % 8));
    writeBlock(currentGroupDesc.bg_inode_bitmap, bitmap);

    super.s_free_inodes_count++;
    currentGroupDesc.bg_free_inodes_count++;
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(currentGroupNum, &currentGroupDesc);
}

void Ext2Shell::freeBlock(unsigned int blockNum) {
    unsigned char bitmap[blockSize];
    readBlock(currentGroupDesc.bg_block_bitmap, bitmap);
    int bit = (blockNum - 1) % super.s_blocks_per_group;
    bitmap[bit / 8] &= ~(1 << (bit % 8));
    writeBlock(currentGroupDesc.bg_block_bitmap, bitmap);

    super.s_free_blocks_count++;
    currentGroupDesc.bg_free_blocks_count++;
    lseek(fd, BASE_OFFSET, SEEK_SET);
    write(fd, &super, sizeof(super));
    writeGroupDesc(currentGroupNum, &currentGroupDesc);
}

void Ext2Shell::cmd_ls() {
    forEachDirEntry(currentInodeNum, [](ext2_dir_entry_2* entry) {
        std::string name(entry->name, entry->name_len);
        std::cout << name << std::endl;
        return true;
    });
}

void Ext2Shell::cmd_cd(const std::string& path) {
    if (path == "..") {
        if (!currentPath.empty()) {
            currentPath.pop_back();
            unsigned int newInodeNum = EXT2_ROOT_INO;
            auto tempPath = currentPath; 
            currentPath.clear();
            updateCurrentDirectory(newInodeNum);
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
    std::string pathStr = "/";
    for (const auto& part : currentPath) {
        pathStr += part + "/";
    }
    if (pathStr.length() > 1 && pathStr.back() == '/') {
        pathStr.pop_back();
    }
    std::cout << pathStr << std::endl;
}

// --- Funções Auxiliares para o `attr` ---

// Converte o modo do inode para string (ex: drwxr-xr--)
std::string modeToString(unsigned short mode) {
    std::string str = "----------";
    if (S_ISDIR(mode)) str[0] = 'd';
    if (S_ISLNK(mode)) str[0] = 'l';
    
    if (mode & S_IRUSR) str[1] = 'r';
    if (mode & S_IWUSR) str[2] = 'w';
    if (mode & S_IXUSR) str[3] = 'x';

    if (mode & S_IRGRP) str[4] = 'r';
    if (mode & S_IWGRP) str[5] = 'w';
    if (mode & S_IXGRP) str[6] = 'x';

    if (mode & S_IROTH) str[7] = 'r';
    if (mode & S_IWOTH) str[8] = 'w';
    if (mode & S_IXOTH) str[9] = 'x';
    
    return str;
}

// Formata o tempo para DD/MM/YYYY HH:MM:SS
std::string formatTime(uint32_t t) {
    if (t == 0) return "N/A";
    std::time_t rawtime = t;
    char buffer[80];
    struct tm* timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", timeinfo);
    return std::string(buffer);
}

// Formata o tamanho para um formato legível (B, KiB, MiB)
std::string formatSize(uint32_t size) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (size >= 1024 * 1024) { // MiB
        ss << (static_cast<double>(size) / (1024 * 1024)) << " MiB";
    } else if (size >= 1024) { // KiB
        ss << (static_cast<double>(size) / 1024) << " KiB";
    } else { // Bytes
        ss << size << " B";
    }
    return ss.str();
}

void Ext2Shell::cmd_attr(const std::string& name) {
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: File or directory '" << name << "' not found." << std::endl;
        return;
    }

    ext2_inode inode;
    readInode(inodeNum, &inode);

    // Imprime o cabeçalho
    std::cout << "permissões uid gid tamanho modificado em" << std::endl;

    // Imprime a linha de dados formatada
    std::cout << modeToString(inode.i_mode) << " "
              << inode.i_uid << " "
              << inode.i_gid << " "
              << formatSize(inode.i_size) << " "
              << formatTime(inode.i_mtime) << std::endl;
}


void Ext2Shell::cmd_cat(const std::string& name) {
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: File '" << name << "' not found." << std::endl;
        return;
    }

    ext2_inode fileInode;
    readInode(inodeNum, &fileInode);

    if (!S_ISREG(fileInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is not a regular file." << std::endl;
        return;
    }

    std::vector<char> buffer(blockSize);
    unsigned int bytesRead = 0;
    const unsigned int fileSize = fileInode.i_size;

    for (int i = 0; i < 12 && bytesRead < fileSize; i++) {
        if (fileInode.i_block[i] == 0) break;
        readBlock(fileInode.i_block[i], buffer.data());
        unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesRead);
        std::cout.write(buffer.data(), bytesToWrite);
        bytesRead += bytesToWrite;
    }
    // ... (Lógica para blocos indiretos continua aqui) ...
    std::cout << std::endl;
}

void Ext2Shell::cmd_touch(const std::string& name) {
    if (name.length() >= EXT2_NAME_LEN) {
        std::cerr << "Error: Name too long." << std::endl;
        return;
    }
    if (getInodeByName(name) != 0) {
        std::cerr << "Error: File '" << name << "' already exists." << std::endl;
        return;
    }
    int newInodeNum = allocateInode();
    if (newInodeNum < 0) {
        std::cerr << "Error: No free inodes available." << std::endl;
        return;
    }

    ext2_inode newInode = {};
    newInode.i_mode = EXT2_S_IFREG | 0644;
    newInode.i_links_count = 1;
    newInode.i_atime = newInode.i_ctime = newInode.i_mtime = (uint32_t)time(nullptr);
    writeInode(newInodeNum, &newInode);

    if (addDirectoryEntry(currentInodeNum, newInodeNum, name, EXT2_FT_REG_FILE) < 0) {
        std::cerr << "Error: Failed to add directory entry." << std::endl;
        freeInode(newInodeNum);
        return;
    }
    std::cout << "File '" << name << "' created successfully." << std::endl;
}

void Ext2Shell::cmd_mkdir(const std::string& name) {
    if (getInodeByName(name) != 0) {
        std::cerr << "Error: File or directory '" << name << "' already exists." << std::endl;
        return;
    }
    int inodeNum = allocateInode();
    if (inodeNum < 0) {
        std::cerr << "Error: No free inodes available." << std::endl;
        return;
    }
    int blockNum = allocateBlock();
    if (blockNum < 0) {
        std::cerr << "Error: No free blocks available." << std::endl;
        freeInode(inodeNum);
        return;
    }

    ext2_inode dirInode = {};
    dirInode.i_mode = EXT2_S_IFDIR | 0755;
    dirInode.i_size = blockSize;
    dirInode.i_links_count = 2;
    dirInode.i_blocks = blockSize / 512;
    dirInode.i_atime = dirInode.i_ctime = dirInode.i_mtime = (uint32_t)time(nullptr);
    dirInode.i_block[0] = blockNum;
    writeInode(inodeNum, &dirInode);

    std::vector<char> blockData(blockSize, 0);
    ext2_dir_entry_2* dotEntry = reinterpret_cast<ext2_dir_entry_2*>(blockData.data());
    dotEntry->inode = inodeNum;
    dotEntry->rec_len = 12;
    dotEntry->name_len = 1;
    dotEntry->file_type = EXT2_FT_DIR;
    dotEntry->name[0] = '.';

    ext2_dir_entry_2* dotDotEntry = reinterpret_cast<ext2_dir_entry_2*>(blockData.data() + 12);
    dotDotEntry->inode = currentInodeNum;
    dotDotEntry->rec_len = blockSize - 12;
    dotDotEntry->name_len = 2;
    dotDotEntry->file_type = EXT2_FT_DIR;
    dotDotEntry->name[0] = '.';
    dotDotEntry->name[1] = '.';
    writeBlock(blockNum, blockData.data());

    if (addDirectoryEntry(currentInodeNum, inodeNum, name, EXT2_FT_DIR) < 0) {
        std::cerr << "Error: Failed to add directory entry." << std::endl;
        freeBlock(blockNum);
        freeInode(inodeNum);
        return;
    }

    currentInode.i_links_count++;
    writeInode(currentInodeNum, &currentInode);
    std::cout << "Directory '" << name << "' created successfully." << std::endl;
}

void Ext2Shell::cmd_rm(const std::string& name) {
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: File '" << name << "' does not exist." << std::endl;
        return;
    }
    ext2_inode targetInode;
    readInode(inodeNum, &targetInode);
    if (S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is a directory. Use 'rmdir'." << std::endl;
        return;
    }

    removeDirectoryEntry(currentInodeNum, name);
    targetInode.i_links_count--;
    if (targetInode.i_links_count == 0) {
        for (int i = 0; i < 12; i++) {
            if (targetInode.i_block[i] != 0) {
                freeBlock(targetInode.i_block[i]);
            }
        }
        freeInode(inodeNum);
    } else {
        writeInode(inodeNum, &targetInode);
    }
    std::cout << "File '" << name << "' removed." << std::endl;
}

void Ext2Shell::cmd_rmdir(const std::string& name) {
    unsigned int inodeNum = getInodeByName(name);
    if (inodeNum == 0) {
        std::cerr << "Error: Directory '" << name << "' does not exist." << std::endl;
        return;
    }
    ext2_inode targetInode;
    readInode(inodeNum, &targetInode);
    if (!S_ISDIR(targetInode.i_mode)) {
        std::cerr << "Error: '" << name << "' is not a directory." << std::endl;
        return;
    }
    if (targetInode.i_links_count > 2) {
        std::cerr << "Error: Directory '" << name << "' is not empty." << std::endl;
        return;
    }
    
    bool isTrulyEmpty = true;
    forEachDirEntry(inodeNum, [&](ext2_dir_entry_2* entry) {
        std::string entryName(entry->name, entry->name_len);
        if (entryName != "." && entryName != "..") {
            isTrulyEmpty = false;
            return false;
        }
        return true;
    });

    if (!isTrulyEmpty) {
        std::cerr << "Error: Directory '" << name << "' is not empty." << std::endl;
        return;
    }

    removeDirectoryEntry(currentInodeNum, name);
    freeBlock(targetInode.i_block[0]);
    freeInode(inodeNum);
    currentInode.i_links_count--;
    writeInode(currentInodeNum, &currentInode);
    std::cout << "Directory '" << name << "' removed." << std::endl;
}

void Ext2Shell::cmd_cp(const std::string& source, const std::string& dest) {
    unsigned int sourceInodeNum = getInodeByName(source);
    if (sourceInodeNum == 0) {
        std::cerr << "Error: Source file '" << source << "' does not exist." << std::endl;
        return;
    }
    ext2_inode sourceInode;
    readInode(sourceInodeNum, &sourceInode);
    if (!S_ISREG(sourceInode.i_mode)) {
        std::cerr << "Error: Source '" << source << "' is not a regular file." << std::endl;
        return;
    }
    std::ofstream outFile(dest, std::ios::out | std::ios::binary);
    if (!outFile.is_open()) {
        std::cerr << "Error: Could not open destination file '" << dest << "'." << std::endl;
        return;
    }

    std::vector<char> buffer(blockSize);
    unsigned int bytesCopied = 0;
    const unsigned int fileSize = sourceInode.i_size;
    forEachDataBlock(sourceInodeNum, [&](const std::vector<char>& block_data) {
        if (bytesCopied >= fileSize) return;
        unsigned int bytesToWrite = std::min((unsigned int)blockSize, fileSize - bytesCopied);
        outFile.write(block_data.data(), bytesToWrite);
        bytesCopied += bytesToWrite;
    });
    outFile.close();
    std::cout << "File '" << source << "' copied to '" << dest << "'." << std::endl;
}

void Ext2Shell::cmd_rename(const std::string& oldName, const std::string& newName) {
    if (newName.length() >= EXT2_NAME_LEN) {
        std::cerr << "Error: New name is too long." << std::endl;
        return;
    }
    unsigned int oldIno = getInodeByName(oldName);
    if (oldIno == 0) {
        std::cerr << "Error: '" << oldName << "' not found." << std::endl;
        return;
    }
    if (getInodeByName(newName) != 0) {
        std::cerr << "Error: '" << newName << "' already exists." << std::endl;
        return;
    }

    ext2_inode targetInode;
    readInode(oldIno, &targetInode);
    unsigned char fileType = S_ISDIR(targetInode.i_mode) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;

    if (addDirectoryEntry(currentInodeNum, oldIno, newName, fileType) < 0) {
        std::cerr << "Error: Could not create new entry." << std::endl;
        return;
    }

    removeDirectoryEntry(currentInodeNum, oldName);
    std::cout << "Renamed '" << oldName << "' to '" << newName << "'." << std::endl;
}
