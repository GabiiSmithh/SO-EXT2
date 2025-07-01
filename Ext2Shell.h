#ifndef EXT2_SHELL_H
#define EXT2_SHELL_H

#ifndef EXT2_S_IFREG
#define EXT2_S_IFREG 0x8000
#endif

#ifndef EXT2_S_IFDIR
#define EXT2_S_IFDIR 0x4000
#endif

#include <string>
#include <vector>
#include <functional>
#include "nEXT2shell.h" // Seu arquivo original com as structs do EXT2

// Constantes e macros movidas para dentro da classe ou usadas diretamente.
#define BASE_OFFSET 1024
#define EXT2_SUPER_MAGIC 0xEF53

class Ext2Shell {
public:
    // O construtor inicializa o sistema de arquivos a partir de uma imagem.
    Ext2Shell(const std::string& imagePath);
    // O destrutor fecha o arquivo da imagem.
    ~Ext2Shell();

    // Inicia o loop principal do shell.
    void run();

private:
    // --- Membros do Estado ---
    int fd; // Descritor do arquivo da imagem
    std::string imagePath;
    ext2_super_block super;
    ext2_group_desc currentGroupDesc;
    ext2_inode currentInode;
    unsigned int currentGroupNum;
    unsigned int currentInodeNum;
    std::vector<std::string> currentPath;
    unsigned int blockSize;

    // --- Métodos Privados de Baixo Nível ---
    void readBlock(unsigned int block, void* buffer);
    void writeBlock(unsigned int block, const void* buffer);
    void readGroupDesc(unsigned int groupNum, ext2_group_desc* group);
    void writeGroupDesc(unsigned int groupNum, const ext2_group_desc* group);
    void readInode(unsigned int inodeNum, ext2_inode* inode);
    void writeInode(unsigned int inodeNum, const ext2_inode* inode);
    
    // Métodos para manipulação de Bitmaps
    bool isBitSet(unsigned char* bitmap, int bit);
    void setBitmapBit(unsigned int bitmapBlockNum, int bit);
    void clearBitmapBit(unsigned int bitmapBlockNum, int bit);
    int findFreeInode();
    int findFreeBlock();

    // Métodos para alocação/desalocação
    int allocateInode();
    int allocateBlock();
    void freeInode(unsigned int inodeNum);
    void freeBlock(unsigned int blockNum);

    // --- Métodos Auxiliares ---
    void initialize();
    void processCommand(const std::string& line);
    std::string getPrompt() const;
    unsigned int getInodeByName(const std::string& name);
    void updateCurrentDirectory(unsigned int inodeNum);
    void forEachDataBlock(unsigned int inodeNum, std::function<void(const std::vector<char>&)> callback);
    void forEachDirEntry(unsigned int dirInodeNum, std::function<bool(ext2_dir_entry_2*)> callback);
    int addDirectoryEntry(unsigned int parentInodeNum, unsigned int childInodeNum, const std::string& name, unsigned char fileType);
    void removeDirectoryEntry(unsigned int parentInodeNum, const std::string& name);

    // --- Implementação dos Comandos ---
    void cmd_info();
    void cmd_ls();
    void cmd_pwd();
    void cmd_cd(const std::string& path);
    void cmd_attr(const std::string& name);
    void cmd_cat(const std::string& name);
    void cmd_touch(const std::string& name);
    void cmd_mkdir(const std::string& name);
    void cmd_rm(const std::string& name);
    void cmd_rmdir(const std::string& name);
    void cmd_cp(const std::string& source, const std::string& destination);
    void cmd_rename(const std::string& oldName, const std::string& newName);
};

#endif // EXT2_SHELL_H