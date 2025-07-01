#ifndef NEXT2SHELL_H
#define NEXT2SHELL_H

// Este arquivo define as estruturas de dados fundamentais do sistema de arquivos EXT2,
// conforme especificado no padrão. Ele serve como um "dicionário" para que o compilador
// entenda como os dados estão organizados na imagem de disco.

// Para compatibilidade e clareza, usamos os tipos de dados de tamanho fixo,
// semelhantes aos usados no kernel do Linux.
#include <stdint.h>

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;

// --- Constantes Importantes ---

#define EXT2_N_BLOCKS               15      // Número de ponteiros de bloco em um inode
#define EXT2_NAME_LEN               255     // Comprimento máximo de um nome de arquivo
#define EXT2_ROOT_INO               2       // O inode do diretório raiz é sempre o 2

// --- Estrutura do Superbloco ---
// Contém metadados globais sobre todo o sistema de arquivos.
struct ext2_super_block {
    __u32   s_inodes_count;         /* Total de inodes no sistema de arquivos */
    __u32   s_blocks_count;         /* Total de blocos no sistema de arquivos */
    __u32   s_r_blocks_count;       /* Número de blocos reservados para o superusuário */
    __u32   s_free_blocks_count;    /* Contagem de blocos livres */
    __u32   s_free_inodes_count;    /* Contagem de inodes livres */
    __u32   s_first_data_block;     /* Bloco onde o superbloco está (0 ou 1) */
    __u32   s_log_block_size;       /* Tamanho do bloco (1024 << s_log_block_size) */
    __u32   s_log_frag_size;        /* Tamanho do fragmento */
    __u32   s_blocks_per_group;     /* Número de blocos por grupo */
    __u32   s_frags_per_group;      /* Número de fragmentos por grupo */
    __u32   s_inodes_per_group;     /* Número de inodes por grupo */
    __u32   s_mtime;                /* Horário da última montagem (mount) */
    __u32   s_wtime;                /* Horário da última escrita */
    __u16   s_mnt_count;            /* Contagem de montagens */
    __u16   s_max_mnt_count;        /* Contagem máxima de montagens antes da verificação */
    __u16   s_magic;                /* Número mágico: 0xEF53 */
    __u16   s_state;                /* Estado do sistema de arquivos */
    __u16   s_errors;               /* Comportamento ao detectar erros */
    __u16   s_minor_rev_level;      /* Nível de revisão menor */
    __u32   s_lastcheck;            /* Horário da última verificação */
    __u32   s_checkinterval;        /* Intervalo máximo entre verificações */
    __u32   s_creator_os;           /* ID do SO que criou o FS */
    __u32   s_rev_level;            /* Nível de revisão */
    __u16   s_def_resuid;           /* UID padrão para blocos reservados */
    __u16   s_def_resgid;           /* GID padrão para blocos reservados */
    __u32   s_first_ino;            /* Primeiro inode não reservado */
    __u16   s_inode_size;           /* Tamanho da estrutura do inode */
    __u16   s_block_group_nr;       /* Número do grupo de blocos deste superbloco */
    __u32   s_feature_compat;       /* Conjunto de recursos compatíveis */
    __u32   s_feature_incompat;     /* Conjunto de recursos incompatíveis */
    __u32   s_feature_ro_compat;    /* Conjunto de recursos somente leitura compatíveis */
    __u8    s_uuid[16];             /* ID único do volume (128 bits) */
    char    s_volume_name[16];      /* Nome do volume */
    char    s_last_mounted[64];     /* Caminho onde foi montado pela última vez */
    __u32   s_algorithm_usage_bitmap; /* Usado para compressão */
    __u8    s_prealloc_blocks;      /* Blocos para pré-alocar */
    __u8    s_prealloc_dir_blocks;  /* Blocos para pré-alocar para diretórios */
    __u16   s_padding1;
    __u8    s_journal_uuid[16];     /* UUID do journal */
    __u32   s_journal_inum;         /* Número do inode do journal */
    __u32   s_journal_dev;          /* Dispositivo do journal */
    __u32   s_last_orphan;          /* Início da lista de inodes órfãos */
    __u32   s_hash_seed[4];         /* Sementes do hash (para diretórios indexados) */
    __u8    s_def_hash_version;     /* Versão do algoritmo de hash padrão */
    __u8    s_reserved_char_pad;
    __u16   s_reserved_word_pad;
    __u32   s_default_mount_opts;
    __u32   s_first_meta_bg;        /* Primeiro grupo de blocos de metadados */
    __u32   s_mkfs_time;            /* Horário de criação do FS */
    __u32   s_jnl_blocks[17];       /* Backup do inode do journal */
    __u32   s_reserved[172];        /* Preenchimento para 1024 bytes */
};

// --- Estrutura do Descritor de Grupo de Blocos ---
// Descreve um único grupo de blocos.
struct ext2_group_desc {
    __u32   bg_block_bitmap;        /* Bloco que contém o bitmap de blocos */
    __u32   bg_inode_bitmap;        /* Bloco que contém o bitmap de inodes */
    __u32   bg_inode_table;         /* Bloco de início da tabela de inodes */
    __u16   bg_free_blocks_count;   /* Contagem de blocos livres no grupo */
    __u16   bg_free_inodes_count;   /* Contagem de inodes livres no grupo */
    __u16   bg_used_dirs_count;     /* Contagem de diretórios no grupo */
    __u16   bg_pad;
    __u32   bg_reserved[3];
};

// --- Estrutura do Inode ---
// Contém todos os metadados de um arquivo ou diretório.
struct ext2_inode {
    __u16   i_mode;                 /* Modo do arquivo (tipo e permissões) */
    __u16   i_uid;                  /* ID do usuário (dono) */
    __u32   i_size;                 /* Tamanho em bytes */
    __u32   i_atime;                /* Horário do último acesso */
    __u32   i_ctime;                /* Horário de criação do inode */
    __u32   i_mtime;                /* Horário da última modificação */
    __u32   i_dtime;                /* Horário de exclusão */
    __u16   i_gid;                  /* ID do grupo */
    __u16   i_links_count;          /* Contagem de hard links */
    __u32   i_blocks;               /* Número de blocos de 512B alocados */
    __u32   i_flags;                /* Flags do arquivo */
    __u32   i_osd1;                 /* Específico do SO 1 */
    __u32   i_block[EXT2_N_BLOCKS]; /* Ponteiros para os blocos de dados */
    __u32   i_generation;           /* Número de geração do arquivo (para NFS) */
    __u32   i_file_acl;             /* ACL do arquivo */
    __u32   i_dir_acl;              /* ACL do diretório */
    __u32   i_faddr;                /* Endereço do fragmento */
    __u8    i_osd2[12];             /* Específico do SO 2 */
};

// --- Constantes para o campo i_mode do Inode ---

// Macros para verificar o tipo de arquivo
#define S_ISDIR(m)  (((m) & 0xF000) == 0x4000) // é um Diretório?
#define S_ISREG(m)  (((m) & 0xF000) == 0x8000) // é um Arquivo Regular?

// Máscaras de permissão
#define EXT2_S_IRUSR 0x0100 // Usuário: read
#define EXT2_S_IWUSR 0x0080 // Usuário: write
#define EXT2_S_IXUSR 0x0040 // Usuário: execute
#define EXT2_S_IRGRP 0x0020 // Grupo: read
#define EXT2_S_IWGRP 0x0010 // Grupo: write
#define EXT2_S_IXGRP 0x0008 // Grupo: execute
#define EXT2_S_IROTH 0x0004 // Outros: read
#define EXT2_S_IWOTH 0x0002 // Outros: write
#define EXT2_S_IXOTH 0x0001 // Outros: execute

// --- Estrutura da Entrada de Diretório ---
// Um diretório é uma lista dessas entradas.
struct ext2_dir_entry_2 {
    __u32   inode;                  /* Número do inode da entrada */
    __u16   rec_len;                /* Comprimento total desta entrada (inclui padding) */
    __u8    name_len;               /* Comprimento do nome do arquivo */
    __u8    file_type;              /* Tipo do arquivo (ver abaixo) */
    char    name[EXT2_NAME_LEN];    /* Nome do arquivo */
};

// --- Constantes para o campo file_type da Entrada de Diretório ---
#define EXT2_FT_UNKNOWN     0
#define EXT2_FT_REG_FILE    1
#define EXT2_FT_DIR         2
#define EXT2_FT_CHRDEV      3
#define EXT2_FT_BLKDEV      4
#define EXT2_FT_FIFO        5
#define EXT2_FT_SOCK        6
#define EXT2_FT_SYMLINK     7

#endif // NEXT2SHELL_H