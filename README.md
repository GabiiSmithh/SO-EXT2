
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
./next2shell minha_imagem.img
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

3.  **(Opcional, mas recomendado)** Adicione alguns arquivos e diretórios à imagem para que você tenha com o que interagir. A ferramenta `debugfs` é perfeita para isso:

    ```bash
    debugfs -w myext2image.img
    ```

    Dentro do `debugfs`, execute os seguintes comandos:

    ```
    # Criar alguns diretórios
    mkdir /home
    mkdir /etc

    # Escrever um arquivo de texto dentro de /home
    # (Este comando pega o arquivo /etc/hostname do seu sistema e o copia para dentro da imagem)
    write /etc/hostname /home/hostname.txt

    # Listar o conteúdo para verificar
    ls -l /home

    # Sair do debugfs
    quit
    ```

    Agora sua imagem `myext2image.img` está pronta para ser usada com o `next2shell` e já contém um diretório e um arquivo.

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
