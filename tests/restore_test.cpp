#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/restore.hpp"
#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("syncvault-restore-test-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::filesystem::path utf8_path(const std::u8string& value)
{
    return std::filesystem::path(value);
}

std::string hash_text(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    return syncvault::to_hex(syncvault::sha256({data, text.size()}));
}

void snapshot_restores_byte_for_byte()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    const auto destination = temporary.path() / "restored";
    const auto documents = utf8_path(u8"文档");
    const auto note = utf8_path(u8"说明.txt");

    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source / documents);
    std::ofstream(source / "data.bin", std::ios::binary) << "abcdefghij";
    std::ofstream(source / documents / note, std::ios::binary)
        << "Unicode path content";
    std::ofstream(source / "empty.bin", std::ios::binary);
    std::filesystem::create_directories(destination);

    const auto snapshot = syncvault::create_snapshot(repository, source, 4U);
    const auto restored = syncvault::restore_snapshot(
        repository, snapshot.snapshot.id, destination);

    require(restored.files_restored == 3U,
            "restore file count is incorrect");
    require(restored.directories_restored == 1U,
            "restore directory count is incorrect");
    require(restored.bytes_restored == 30U,
            "restore byte count is incorrect");
    require(read_text(destination / "data.bin") == "abcdefghij",
            "binary file was not restored byte-for-byte");
    require(read_text(destination / documents / note) == "Unicode path content",
            "file under a Unicode path was not restored");
    require(std::filesystem::file_size(destination / "empty.bin") == 0U,
            "empty file was not restored");
}

void nonempty_destination_is_rejected()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    const auto destination = temporary.path() / "destination";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source);
    std::ofstream(source / "file.txt", std::ios::binary) << "data";
    const auto snapshot = syncvault::create_snapshot(repository, source, 4U);
    std::filesystem::create_directories(destination);
    std::ofstream(destination / "keep.txt", std::ios::binary) << "keep";

    bool failed = false;
    try {
        static_cast<void>(syncvault::restore_snapshot(
            repository, snapshot.snapshot.id, destination));
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed, "nonempty restore destination should be rejected");
    require(read_text(destination / "keep.txt") == "keep",
            "existing destination content should remain untouched");
}

void corrupted_chunk_aborts_atomic_restore()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    const auto destination = temporary.path() / "destination";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source);
    std::ofstream(source / "file.txt", std::ios::binary) << "data";
    const auto snapshot = syncvault::create_snapshot(repository, source, 4U);
    const auto manifest = syncvault::read_snapshot_manifest(
        repository, snapshot.snapshot.id);
    const auto stored_path = syncvault::chunk_path(
        repository, manifest.entries.front().chunks.front().digest);
    std::ofstream(stored_path, std::ios::binary | std::ios::trunc) << "xxxx";

    bool failed = false;
    try {
        static_cast<void>(syncvault::restore_snapshot(
            repository, snapshot.snapshot.id, destination));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "corrupted chunks should abort restore");
    require(!std::filesystem::exists(destination),
            "failed atomic restore should not publish a destination");

    for (const auto& entry : std::filesystem::directory_iterator(
             temporary.path())) {
        require(entry.path().filename().string().find(
                    "destination.syncvault-restore-")
                    == std::string::npos,
                "failed restore should clean its staging directory");
    }
}

void path_traversal_manifest_is_rejected()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto destination = temporary.path() / "destination";
    syncvault::Repository::initialize(repository);

    const std::string body =
        "created_at_ns=1\n"
        "source=43\n"
        "files=1\n"
        "directories=0\n"
        "total_bytes=0\n\n"
        "F\t0\t0\t2e2e2f6576696c2e747874\t\n";
    const auto id = "1-" + hash_text(body).substr(0U, 12U);
    std::ofstream(repository / "snapshots" / (id + ".svsnap"),
                  std::ios::binary)
        << "format=1\nid=" << id << '\n' << body;

    bool failed = false;
    try {
        static_cast<void>(syncvault::restore_snapshot(
            repository, id, destination));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "path traversal in a manifest should be rejected");
    require(!std::filesystem::exists(temporary.path() / "evil.txt"),
            "path traversal must not write outside the destination");
}

}  // namespace

int main()
{
    try {
        snapshot_restores_byte_for_byte();
        nonempty_destination_is_rejected();
        corrupted_chunk_aborts_atomic_restore();
        path_traversal_manifest_is_rejected();
        std::cout << "All restore tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
