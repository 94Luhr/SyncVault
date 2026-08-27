#include "syncvault/repository.hpp"
#include "syncvault/snapshot.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
            / ("syncvault-snapshot-test-" + std::to_string(unique));
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

void snapshots_capture_tree_and_reuse_chunks()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source / "docs");
    std::ofstream(source / "hello.txt", std::ios::binary) << "hello";
    std::ofstream(source / "docs" / "note.txt", std::ios::binary) << "world!";
    std::ofstream(source / "empty.bin", std::ios::binary);

    const auto first = syncvault::create_snapshot(repository, source, 4U);
    require(first.snapshot.file_count == 3U, "snapshot file count is incorrect");
    require(first.snapshot.directory_count == 1U,
            "snapshot directory count is incorrect");
    require(first.snapshot.total_bytes == 11U,
            "snapshot total byte count is incorrect");
    require(first.chunks_written == 4U && first.chunks_reused == 0U,
            "first snapshot should write four chunks");
    require(first.bytes_written == 11U,
            "first snapshot written bytes are incorrect");

    const auto first_manifest = repository / "snapshots"
        / (first.snapshot.id + ".svsnap");
    require(std::filesystem::is_regular_file(first_manifest),
            "snapshot manifest was not published");
    require(std::filesystem::is_empty(repository / "tmp"),
            "snapshot temporary files should be cleaned up");

    const auto second = syncvault::create_snapshot(repository, source, 4U);
    require(second.snapshot.id != first.snapshot.id,
            "each snapshot should receive a unique ID");
    require(second.chunks_written == 0U && second.chunks_reused == 4U,
            "unchanged snapshot should reuse all chunks");
    require(second.bytes_written == 0U,
            "unchanged snapshot should write no content bytes");

    const auto listed = syncvault::list_snapshots(repository);
    require(listed.size() == 2U, "two snapshots should be listed");
    require(listed.front().id == second.snapshot.id,
            "snapshots should be listed newest first");
    require(listed.back().id == first.snapshot.id,
            "older snapshot is missing from the list");
    require(listed.front().file_count == 3U
                && listed.front().directory_count == 1U
                && listed.front().total_bytes == 11U,
            "listed snapshot summary is incorrect");
}

void repository_inside_source_is_rejected()
{
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "source";
    const auto repository = source / "repository";
    std::filesystem::create_directories(source);
    syncvault::Repository::initialize(repository);

    bool failed = false;
    try {
        static_cast<void>(syncvault::create_snapshot(repository, source, 4U));
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed, "a repository inside its source should be rejected");
}

void malformed_manifest_is_rejected()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    syncvault::Repository::initialize(repository);
    std::ofstream(repository / "snapshots" / "broken.svsnap",
                  std::ios::binary)
        << "not-a-snapshot\n";

    bool failed = false;
    try {
        static_cast<void>(syncvault::list_snapshots(repository));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "malformed snapshot manifests should be rejected");
}

}  // namespace

int main()
{
    try {
        snapshots_capture_tree_and_reuse_chunks();
        repository_inside_source_is_rejected();
        malformed_manifest_is_rejected();
        std::cout << "All snapshot tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
