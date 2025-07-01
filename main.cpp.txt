#include "Ext2Shell.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <image_file.img>" << std::endl;
        return 1;
    }

    try {
        Ext2Shell shell(argv[1]);
        shell.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}