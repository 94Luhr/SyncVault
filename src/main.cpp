#include "syncvault/chunker.hpp"
#include "syncvault/chunk_store.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/scanner.hpp"
#include "syncvault/sha256.hpp"
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
        << "  syncvault chunks <file>\n"
        << "  syncvault init <repository>\n"
        << "  syncvault scan <source>\n"
        << "  syncvault store <repository> <file>\n"
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

        if (argc == 3 && std::string_view(argv[1]) == "chunks") {
            const auto chunks = syncvault::chunk_file(
                std::filesystem::u8path(argv[2]));

            std::cout << "INDEX\tOFFSET\tSIZE\tSHA256\n";
            for (std::size_t index = 0; index < chunks.size(); ++index) {
                const auto& chunk = chunks[index];
                std::cout << index << '\t'
                          << chunk.offset << '\t'
                          << chunk.size << '\t'
                          << syncvault::to_hex(chunk.digest) << '\n';
            }
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "init") {
            const auto repository = syncvault::Repository::initialize(
                std::filesystem::u8path(argv[2]));
            std::cout << "Initialized SyncVault repository at "
                      << repository.root().string() << '\n';
            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "scan") {
            const auto entries = syncvault::scan_directory(
                std::filesystem::u8path(argv[2]));

            std::cout << "TYPE\tSIZE\tMODIFIED_NS\tPATH\n";
            for (const auto& entry : entries) {
                std::cout << syncvault::to_string(entry.type) << '\t'
                          << entry.size << '\t'
                          << entry.modified_time_ns << '\t'
                          << syncvault::path_to_utf8(entry.relative_path) << '\n';
            }
            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "store") {
            const auto result = syncvault::store_file_chunks(
                std::filesystem::u8path(argv[2]),
                std::filesystem::u8path(argv[3]));

            std::cout << "INDEX\tOFFSET\tSIZE\tSHA256\n";
            for (std::size_t index = 0; index < result.chunks.size(); ++index) {
                const auto& chunk = result.chunks[index];
                std::cout << index << '\t'
                          << chunk.offset << '\t'
                          << chunk.size << '\t'
                          << syncvault::to_hex(chunk.digest) << '\n';
            }
            std::cout << "Stored " << result.chunks_written
                      << " new chunk(s), reused " << result.chunks_reused
                      << ", wrote " << result.bytes_written << " byte(s)\n";
            return 0;
        }

        print_usage();
        return argc == 1 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
