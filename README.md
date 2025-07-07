
# nEXT2 Shell: Um Shell Interativo para Sistemas de Arquivos EXT2

`nEXT2 Shell` é um projeto acadêmico que implementa um shell de linha de comando para interagir diretamente com uma imagem de disco (`.img`) formatada com o sistema de arquivos EXT2. Em vez de usar o sistema operacional para gerenciar arquivos, este programa lê e escreve os bytes da imagem diretamente, manipulando estruturas como o superbloco, descritores de grupo, inodes e entradas de diretório.

O objetivo principal é o aprendizado prático sobre a organização e o funcionamento interno de um sistema de arquivos do tipo Unix.

## ✨ Funcionalidades

  - Navegação no sistema de arquivos (`cd`, `ls`, `pwd`).
  - Leitura de arquivos e atributos (`cat`, `attr`).
  - Criação de arquivos e diretórios (`touch`, `mkdir`).
  - Remoção de arquivos e diretórios (`rm`, `rmdir`).
  - Renomear e copiar arquivos (`rename`, `cp`).
  - Exibição de informações gerais do sistema de arquivos (`info`).

## 🛠️ Tecnologias Utilizadas

  - **C++17**: Para uma implementação moderna, organizada e segura.
  - **Makefile**: Para automação do processo de compilação.
  - **Readline**: Biblioteca GNU para fornecer uma experiência de terminal mais rica (histórico de comandos, edição de linha).

## 🚀 Como Compilar e Executar

### Pré-requisitos

Antes de começar, você precisa ter os seguintes pacotes instalados no seu sistema (exemplos para Debian/Ubuntu):

  - Um compilador C++ e o `make`:
    ```bash
    sudo apt-get update
    sudo apt-get install build-essential
    ```
  - A biblioteca de desenvolvimento do Readline:
    ```bash
    sudo apt-get install libreadline-dev
    ```

### Compilação

Com os pré-requisitos instalados, basta clonar (ou ter os arquivos em um diretório) e usar o `Makefile` fornecido.

1.  Navegue até o diretório raiz do projeto no seu terminal.
2.  Execute o comando `make`:
    ```bash
    make
    ```
    Isso irá compilar todos os arquivos-fonte e gerar um executável chamado `next2shell`.

### Execução

Para iniciar o shell, execute o programa passando o caminho para a sua imagem de disco como argumento:

```bash
./next2shell myext2image.img
```

## 📦 Como Criar uma Imagem EXT2 para Testes

Se você não tiver uma imagem `.img`, pode criar uma facilmente no Linux:

1.  **Crie um arquivo vazio de 20MB:**

    ```bash
    dd if=/dev/zero of=myext2image.img bs=1M count=20
    ```

2.  **Formate este arquivo com o sistema de arquivos EXT2:**

    ```bash
    mkfs.ext2 -F myext2image.img
    ```

## Criação e geração da imagem de volume ext2

Gerando imagens ext2 (64MiB com blocos de 1K):
```console
# dd if=/dev/zero of=./myext2image.img bs=1024 count=64K
# mkfs.ext2 -b 1024 ./myext2image.img
```

Verificando a integridade de um sistema ext2:
```console
# e2fsck myext2image.img
```

Montando a imagem do volume com ext2:
```console
# sudo mount myext2image.img /mnt
```

Estrutura original de arquivos do volume (comando `tree` via bash):
```
/
├── [1.0K]  documentos
│   ├── [1.0K]  emptydir
│   ├── [9.2K]  alfabeto.txt
│   └── [   0]  vazio.txt
├── [1.0K]  imagens
│   ├── [8.1M]  one_piece.jpg
│   ├── [391K]  saber.jpg
│   └── [ 11M]  toscana_puzzle.jpg
├── [1.0K]  livros
│   ├── [1.0K]  classicos
│   │   ├── [506K]  A Journey to the Centre of the Earth - Jules Verne.txt
│   │   ├── [409K]  Dom Casmurro - Machado de Assis.txt
│   │   ├── [861K]  Dracula-Bram_Stoker.txt
│   │   ├── [455K]  Frankenstein-Mary_Shelley.txt
│   │   └── [232K]  The Worderful Wizard of Oz - L. Frank Baum.txt
│   └── [1.0K]  religiosos
│       └── [3.9M]  Biblia.txt
├── [ 12K]  lost+found
└── [  29]  hello.txt

```

Informações de espaço  (comando `df` via bash):
```
Blocos de 1k: 62186
Usado: 26777 KiB
Disponível: 32133 KiB
```

Desmontando a imagem do volume com ext2:
```console
# sudo umount /mnt
```

## 📋 Comandos Disponíveis

| Comando  | Sintaxe                               | Descrição                                                                                                    |
| :------- | :------------------------------------ | :------------------------------------------------------------------------------------------------------------- |
| `info`   | `info`                                | Exibe informações gerais do superbloco (tamanho, inodes livres, etc.).                                         |
| `ls`     | `ls`                                  | Lista os arquivos e diretórios do diretório corrente.                                                          |
| `cd`     | `cd <caminho>`                        | Altera o diretório corrente para o `<caminho>` especificado (use `.` para o atual e `..` para o pai).          |
| `pwd`    | `pwd`                                 | Exibe o caminho absoluto do diretório corrente.                                                                |
| `attr`   | `attr <arquivo/dir>`                  | Mostra os atributos (permissões, tamanho, datas) do inode do item especificado.                                |
| `cat`    | `cat <arquivo>`                       | Exibe o conteúdo de `<arquivo>` no terminal.                                                                   |
| `touch`  | `touch <arquivo>`                     | Cria um novo arquivo vazio com o nome especificado.                                                            |
| `mkdir`  | `mkdir <diretorio>`                   | Cria um novo diretório vazio com o nome especificado.                                                          |
| `rm`     | `rm <arquivo>`                        | Remove o arquivo especificado.                                                                                 |
| `rmdir`  | `rmdir <diretorio>`                   | Remove o diretório especificado, somente se ele estiver vazio.                                                 |
| `cp`     | `cp <origem_na_imagem> <destino_local>` | **Copia para fora:** Copia um arquivo de dentro da imagem para o seu sistema de arquivos local.                |
| `rename` | `rename <nome_antigo> <nome_novo>`    | Renomeia um arquivo ou diretório dentro do diretório corrente.                                                 |
| `exit`   | `exit`                                | Encerra a execução do shell.                                                                                   |

## 📂 Estrutura do Projeto

```
.
├── Ext2Shell.cpp         # Implementação da classe do shell
├── Ext2Shell.h           # Interface (header) da classe do shell
├── main.cpp              # Ponto de entrada principal do programa
├── Makefile              # Arquivo de automação da compilação
├── nEXT2shell.h          # Definições das estruturas de dados do EXT2
└── README.md             # Este arquivo
```

## 📄 Licença

Este projeto está sob a licença MIT. Veja o arquivo `LICENSE` para mais detalhes (você pode criar um se desejar).
