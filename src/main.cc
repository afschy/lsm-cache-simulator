#include <iostream>
#include <zstd_reader.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout<< "Usage: bin/lsm-sim filename\n";
        exit(0);
    }

    ZSTDReader raw_file_reader(argv[1]);
    std::string line;
    
    raw_file_reader.get_next_line(&line);
    std::cout << line << "\n";
    raw_file_reader.goto_line(1000000);
    raw_file_reader.get_next_line(&line);
    std::cout << line << "\n";
}
