#include "syncvault/chunker.hpp"
#include "syncvault/chunk_store.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/scanner.hpp"
#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"
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
        << "  syncvault snapshot create <repository> <source>\n"
        << "  syncvault snapshot list <repository>\n"
        << "  syncvault store <repository> <file>\n"
        << "  syncvault version\n";
}

template <typename Character>
bool argument_equals(const Character* argument, std::string_view expected)
{
    const std::basic_string_view<Character> value(argument);
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != static_cast<Character>(expected[index])) {
            return false;
        }
    }
    return true;
}

template <typename Character>
int run_cli(int argc, Character* argv[])
{
    try {
        if (argc == 2 && argument_equals(argv[1], "version")) {
            std::cout << "SyncVault " << syncvault::version << '\n';
            return 0;
        }

        if (argc == 3 && argument_equals(argv[1], "chunks")) {
            const auto chunks = syncvault::chunk_file(
                std::filesystem::path(argv[2]));

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

        if (argc == 3 && argument_equals(argv[1], "init")) {
            const auto repository = syncvault::Repository::initialize(
                std::filesystem::path(argv[2]));
            std::cout << "Initialized SyncVault repository at "
                      << repository.root().string() << '\n';
            return 0;
        }

        if (argc == 3 && argument_equals(argv[1], "scan")) {
            const auto entries = syncvault::scan_directory(
                std::filesystem::path(argv[2]));

            std::cout << "TYPE\tSIZE\tMODIFIED_NS\tPATH\n";
            for (const auto& entry : entries) {
                std::cout << syncvault::to_string(entry.type) << '\t'
                          << entry.size << '\t'
                          << entry.modified_time_ns << '\t'
                          << syncvault::path_to_utf8(entry.relative_path) << '\n';
            }
            return 0;
        }

        if (argc == 4 && argument_equals(argv[1], "store")) {
            const auto result = syncvault::store_file_chunks(
                std::filesystem::path(argv[2]),
                std::filesystem::path(argv[3]));

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

        if (argc == 5 && argument_equals(argv[1], "snapshot")
            && argument_equals(argv[2], "create")) {
            const auto result = syncvault::create_snapshot(
                std::filesystem::path(argv[3]),
                std::filesystem::path(argv[4]));
            std::cout << "Created snapshot " << result.snapshot.id << " with "
                      << result.snapshot.file_count << " file(s), "
                      << result.snapshot.directory_count << " directory(s), "
                      << result.snapshot.total_bytes << " byte(s)\n"
                      << "Stored " << result.chunks_written
                      << " new chunk(s), reused " << result.chunks_reused
                      << ", wrote " << result.bytes_written << " byte(s)\n";
            return 0;
        }

        if (argc == 4 && argument_equals(argv[1], "snapshot")
            && argument_equals(argv[2], "list")) {
            const auto snapshots = syncvault::list_snapshots(
                std::filesystem::path(argv[3]));
            std::cout << "CREATED_NS\tFILES\tDIRECTORIES\tBYTES\tID\n";
            for (const auto& snapshot : snapshots) {
                std::cout << snapshot.created_at_ns << '\t'
                          << snapshot.file_count << '\t'
                          << snapshot.directory_count << '\t'
                          << snapshot.total_bytes << '\t'
                          << snapshot.id << '\n';
            }
            return 0;
        }

        print_usage();
        return argc == 1 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    return run_cli(argc, argv);
}
