#include "platform_file.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("tpv-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
        fs::u8path(u8"Save files ÆØÅ");
    try {
        fs::create_directories(root);
        const auto temporary = root / "replay.tmp", destination = root / "replay.pvr";
        std::ofstream(destination) << "previous save";
        std::ofstream(temporary) << "updated save";
        if (!sjp::replaceSavedFile(temporary, destination) || fs::exists(temporary))
            throw std::runtime_error("Cannot replace an existing save");
        std::ifstream file(destination);
        std::string contents;
        std::getline(file, contents);
        file.close();
        if (contents != "updated save")
            throw std::runtime_error("Replacement lost the completed save");
        fs::create_directory(root / "blocked");
        std::ofstream(root / "blocked/keep") << "preserved";
        std::ofstream(temporary) << "new save";
        if (sjp::replaceSavedFile(temporary, root / "blocked") || !fs::exists(temporary) ||
            !fs::exists(root / "blocked/keep"))
            throw std::runtime_error("A failed replacement destroyed existing data");
        fs::remove_all(root.parent_path());
        std::cout << "Save replacement, failure preservation and Unicode paths passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root.parent_path(), ignored);
        return 1;
    }
}
