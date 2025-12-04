#include "dwg_inspector.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: dwg_inspect <file.dwg>\n";
        return 1;
    }

    const std::string path = argv[1];

    try {
        DwgInspector inspector;
        nlohmann::json j = inspector.inspect(path);
        std::cout << j.dump(2) << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
