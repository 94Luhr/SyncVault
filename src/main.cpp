#include "syncvault/repository.hpp"
#include "syncvault/version.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage()
{
    std::cout
        << "SyncVault " << syncvault::version << "\n\n"
        << "Usage:\n"
        << "  syncvault init <repository>\n"
        << "  syncvault version\n";
}

}  // namespace

int main(int argc, char* argv[])
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "version") {
            std::cout << "SyncVault " << syncvault::version << '\n';
            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "init") {
            const auto repository = syncvault::Repository::initialize(
                std::filesystem::u8path(argv[2]));
            std::cout << "Initialized SyncVault repository at "
                      << repository.root().string() << '\n';
            return 0;
        }

        print_usage();
        return argc == 1 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
