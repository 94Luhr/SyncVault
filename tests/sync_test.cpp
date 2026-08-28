#include "syncvault/repository.hpp"
#include "syncvault/restore.hpp"
#include "syncvault/snapshot.hpp"
#include "syncvault/sync.hpp"
#include "syncvault/verify.hpp"

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
            / ("syncvault-sync-test-" + std::to_string(unique));
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

void repositories_synchronize_incrementally()
{
    TemporaryDirectory temporary;
    const auto source_repository = temporary.path() / "source-repository";
    const auto destination_repository = temporary.path() / "destination-repository";
    const auto source_data = temporary.path() / "source-data";
    syncvault::Repository::initialize(source_repository);
    syncvault::Repository::initialize(destination_repository);
    std::filesystem::create_directories(source_data);
    std::ofstream(source_data / "data.bin", std::ios::binary) << "abcdefghij";
    const auto first_snapshot = syncvault::create_snapshot(
        source_repository, source_data, 4U);

    const auto plan = syncvault::plan_synchronization(
        source_repository, destination_repository);
    require(plan.chunks_to_copy == 3U && plan.chunks_to_reuse == 0U,
            "initial plan should include every chunk");
    require(plan.snapshots_to_copy == 1U && plan.snapshots_to_reuse == 0U,
            "initial plan should include its snapshot");
    require(plan.content_bytes_to_copy == 10U,
            "initial plan byte count is incorrect");
    require(std::filesystem::is_empty(destination_repository / "chunks")
                && std::filesystem::is_empty(
                    destination_repository / "snapshots"),
            "planning must not modify the destination repository");

    const auto first = syncvault::synchronize_repositories(
        source_repository, destination_repository);
    require(first.chunks_copied == 3U && first.chunks_reused == 0U,
            "first synchronization should copy all chunks");
    require(first.snapshots_copied == 1U && first.snapshots_reused == 0U,
            "first synchronization should copy its snapshot");
    require(first.content_bytes_copied == 10U,
            "first synchronization byte count is incorrect");
    require(syncvault::verify_repository(destination_repository).healthy(),
            "synchronized destination should pass verification");

    const auto restored = temporary.path() / "restored";
    static_cast<void>(syncvault::restore_snapshot(
        destination_repository, first_snapshot.snapshot.id, restored));
    require(read_text(restored / "data.bin") == "abcdefghij",
            "synchronized snapshot should restore byte-for-byte");

    const auto repeated = syncvault::synchronize_repositories(
        source_repository, destination_repository);
    require(repeated.chunks_copied == 0U && repeated.chunks_reused == 3U,
            "repeated synchronization should reuse every chunk");
    require(repeated.snapshots_copied == 0U
                && repeated.snapshots_reused == 1U,
            "repeated synchronization should reuse its snapshot");
    const auto repeated_plan = syncvault::plan_synchronization(
        source_repository, destination_repository);
    require(repeated_plan.chunks_to_copy == 0U
                && repeated_plan.chunks_to_reuse == 3U
                && repeated_plan.snapshots_to_copy == 0U
                && repeated_plan.snapshots_to_reuse == 1U,
            "repeated plan should require no transfer");

    std::ofstream(source_data / "data.bin", std::ios::binary | std::ios::trunc)
        << "abcdWXYZij";
    const auto second_snapshot = syncvault::create_snapshot(
        source_repository, source_data, 4U);
    const auto incremental = syncvault::synchronize_repositories(
        source_repository, destination_repository);
    require(incremental.chunks_copied == 1U
                && incremental.chunks_reused == 3U,
            "incremental synchronization should copy only the changed chunk");
    require(incremental.snapshots_copied == 1U
                && incremental.snapshots_reused == 1U,
            "incremental synchronization should copy only the new snapshot");

    const auto second_restore = temporary.path() / "second-restore";
    static_cast<void>(syncvault::restore_snapshot(
        destination_repository, second_snapshot.snapshot.id, second_restore));
    require(read_text(second_restore / "data.bin") == "abcdWXYZij",
            "incrementally synchronized snapshot is incorrect");
}

void corrupted_source_is_rejected_before_copy()
{
    TemporaryDirectory temporary;
    const auto source_repository = temporary.path() / "source-repository";
    const auto destination_repository = temporary.path() / "destination-repository";
    const auto source_data = temporary.path() / "source-data";
    syncvault::Repository::initialize(source_repository);
    syncvault::Repository::initialize(destination_repository);
    std::filesystem::create_directories(source_data);
    std::ofstream(source_data / "data.bin", std::ios::binary) << "data";
    static_cast<void>(
        syncvault::create_snapshot(source_repository, source_data, 4U));
    std::filesystem::path chunk_path;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             source_repository / "chunks")) {
        if (entry.is_regular_file()) {
            chunk_path = entry.path();
            break;
        }
    }
    require(!chunk_path.empty(), "test chunk was not created");
    std::ofstream(chunk_path, std::ios::binary | std::ios::trunc) << "xxxx";

    bool failed = false;
    try {
        static_cast<void>(syncvault::synchronize_repositories(
            source_repository, destination_repository));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "corrupted source repository should be rejected");
    require(std::filesystem::is_empty(destination_repository / "snapshots"),
            "failed synchronization should not publish snapshots");
}

}  // namespace

int main()
{
    try {
        repositories_synchronize_incrementally();
        corrupted_source_is_rejected_before_copy();
        std::cout << "All synchronization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
