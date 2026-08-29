#include "syncvault/chunker.hpp"
#include "syncvault/chunk_store.hpp"
#include "syncvault/network.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/restore.hpp"
#include "syncvault/scanner.hpp"
#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"
#include "syncvault/sync.hpp"
#include "syncvault/version.hpp"
#include "syncvault/verify.hpp"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void print_usage()
{
    std::cout
        << "SyncVault " << syncvault::version << "\n\n"
        << "Usage:\n"
        << "  syncvault chunks <file>\n"
        << "  syncvault init <repository>\n"
        << "  syncvault ping <host> <port>\n"
        << "  syncvault scan <source>\n"
        << "  syncvault serve --once <repository> <port>\n"
        << "  syncvault snapshot create <repository> <source>\n"
        << "  syncvault snapshot list <repository>\n"
        << "  syncvault snapshot restore <repository> <id> <destination>\n"
        << "  syncvault store <repository> <file>\n"
        << "  syncvault sync --dry-run <source-repository> <destination-repository>\n"
        << "  syncvault sync <source-repository> <destination-repository>\n"
        << "  syncvault verify <repository>\n"
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
std::uint16_t parse_port(const Character* value)
{
    std::uint32_t port = 0U;
    if (*value == static_cast<Character>(0)) {
        throw std::invalid_argument("port must be between 1 and 65535");
    }
    for (; *value != static_cast<Character>(0); ++value) {
        if (*value < static_cast<Character>('0')
            || *value > static_cast<Character>('9')) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
        port = port * 10U
            + static_cast<std::uint32_t>(*value - static_cast<Character>('0'));
        if (port > 65'535U) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
    }
    if (port == 0U) {
        throw std::invalid_argument("port must be between 1 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

template <typename Character>
int run_cli(int argc, Character* argv[])
{
    try {
        if (argc == 2 && argument_equals(argv[1], "version")) {
            std::cout << "SyncVault " << syncvault::version << '\n';
            return 0;
        }

        if (argc == 4 && argument_equals(argv[1], "ping")) {
            const auto host = std::filesystem::path(argv[2]).string();
            const auto result = syncvault::perform_protocol_handshake(
                host, parse_port(argv[3]));
            std::cout << "Connected to " << result.peer
                      << " using SyncVault protocol "
                      << result.protocol_version << '\n';
            return 0;
        }

        if (argc == 5 && argument_equals(argv[1], "serve")
            && argument_equals(argv[2], "--once")) {
            syncvault::ProtocolHandshakeServer server(
                std::filesystem::path(argv[3]), parse_port(argv[4]));
            std::cout << "Listening on 127.0.0.1:" << server.local_port()
                      << '\n';
            const auto result = server.accept_once();
            std::cout << "Accepted " << result.peer
                      << " using SyncVault protocol "
                      << result.protocol_version << '\n';
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

        if (argc == 6 && argument_equals(argv[1], "snapshot")
            && argument_equals(argv[2], "restore")) {
            const auto result = syncvault::restore_snapshot(
                std::filesystem::path(argv[3]),
                std::filesystem::path(argv[4]).string(),
                std::filesystem::path(argv[5]));
            std::cout << "Restored " << result.files_restored << " file(s), "
                      << result.directories_restored << " directory(s), "
                      << result.bytes_restored << " byte(s)\n";
            return 0;
        }

        if (argc == 3 && argument_equals(argv[1], "verify")) {
            const auto result = syncvault::verify_repository(
                std::filesystem::path(argv[2]));
            for (const auto& issue : result.issues) {
                std::cout << syncvault::to_string(issue.kind) << '\t'
                          << issue.object << '\t' << issue.message << '\n';
            }
            std::cout << (result.healthy() ? "OK" : "FAILED") << ": checked "
                      << result.snapshots_checked << " snapshot(s), "
                      << result.chunks_checked << " chunk(s), "
                      << result.bytes_checked << " byte(s); "
                      << result.unreferenced_chunks
                      << " unreferenced chunk(s), " << result.issues.size()
                      << " issue(s)\n";
            return result.healthy() ? 0 : 1;
        }

        if (argc == 5 && argument_equals(argv[1], "sync")
            && argument_equals(argv[2], "--dry-run")) {
            const auto plan = syncvault::plan_synchronization(
                std::filesystem::path(argv[3]),
                std::filesystem::path(argv[4]));
            std::cout << "Would copy " << plan.chunks_to_copy
                      << " chunk(s), reuse " << plan.chunks_to_reuse
                      << "; copy " << plan.snapshots_to_copy
                      << " snapshot(s), reuse " << plan.snapshots_to_reuse
                      << "; transfer " << plan.content_bytes_to_copy
                      << " content byte(s) and "
                      << plan.manifest_bytes_to_copy
                      << " manifest byte(s)\n";
            return 0;
        }

        if (argc == 4 && argument_equals(argv[1], "sync")) {
            const auto result = syncvault::synchronize_repositories(
                std::filesystem::path(argv[2]),
                std::filesystem::path(argv[3]));
            std::cout << "Copied " << result.chunks_copied
                      << " chunk(s), reused " << result.chunks_reused
                      << "; copied " << result.snapshots_copied
                      << " snapshot(s), reused " << result.snapshots_reused
                      << "; transferred " << result.content_bytes_copied
                      << " content byte(s) and "
                      << result.manifest_bytes_copied
                      << " manifest byte(s)\n";
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
