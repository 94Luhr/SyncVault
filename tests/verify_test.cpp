#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/snapshot.hpp"
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
            / ("syncvault-verify-test-" + std::to_string(unique));
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

bool has_issue(const syncvault::VerificationResult& result,
               syncvault::VerificationIssueKind kind)
{
    for (const auto& issue : result.issues) {
        if (issue.kind == kind) {
            return true;
        }
    }
    return false;
}

void healthy_repository_passes_verification()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source);
    std::ofstream(source / "data.bin", std::ios::binary) << "abcdefghij";
    syncvault::create_snapshot(repository, source, 4U);

    const auto result = syncvault::verify_repository(repository);
    require(result.healthy(), "healthy repository should pass verification");
    require(result.snapshots_checked == 1U,
            "verification snapshot count is incorrect");
    require(result.chunks_checked == 3U,
            "verification chunk count is incorrect");
    require(result.bytes_checked == 10U,
            "verification byte count is incorrect");
    require(result.unreferenced_chunks == 0U,
            "referenced chunks should not be counted as unreferenced");
}

void corrupted_chunk_is_reported()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source);
    std::ofstream(source / "data.bin", std::ios::binary) << "data";
    const auto snapshot = syncvault::create_snapshot(repository, source, 4U);
    const auto manifest = syncvault::read_snapshot_manifest(
        repository, snapshot.snapshot.id);
    const auto path = syncvault::chunk_path(
        repository, manifest.entries.front().chunks.front().digest);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "xxxx";

    const auto result = syncvault::verify_repository(repository);
    require(!result.healthy(), "corrupted repository should fail verification");
    require(has_issue(result,
                      syncvault::VerificationIssueKind::chunk_digest_mismatch),
            "corrupted chunk digest should be reported");
}

void missing_chunk_is_reported()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "source";
    syncvault::Repository::initialize(repository);
    std::filesystem::create_directories(source);
    std::ofstream(source / "data.bin", std::ios::binary) << "data";
    const auto snapshot = syncvault::create_snapshot(repository, source, 4U);
    const auto manifest = syncvault::read_snapshot_manifest(
        repository, snapshot.snapshot.id);
    std::filesystem::remove(syncvault::chunk_path(
        repository, manifest.entries.front().chunks.front().digest));

    const auto result = syncvault::verify_repository(repository);
    require(has_issue(result, syncvault::VerificationIssueKind::missing_chunk),
            "missing chunk should be reported");
}

void malformed_manifest_is_reported()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    syncvault::Repository::initialize(repository);
    std::ofstream(repository / "snapshots" / "broken.svsnap",
                  std::ios::binary)
        << "not-a-manifest\n";

    const auto result = syncvault::verify_repository(repository);
    require(has_issue(result,
                      syncvault::VerificationIssueKind::invalid_manifest),
            "malformed manifest should be reported");
}

void valid_unreferenced_chunk_is_counted_without_failure()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    const auto source = temporary.path() / "data.bin";
    syncvault::Repository::initialize(repository);
    std::ofstream(source, std::ios::binary) << "orphan";
    syncvault::store_file_chunks(repository, source, 4U);

    const auto result = syncvault::verify_repository(repository);
    require(result.healthy(), "valid unreferenced chunks are not corruption");
    require(result.unreferenced_chunks == 2U,
            "all unreferenced chunks should be counted");
}

}  // namespace

int main()
{
    try {
        healthy_repository_passes_verification();
        corrupted_chunk_is_reported();
        missing_chunk_is_reported();
        malformed_manifest_is_reported();
        valid_unreferenced_chunk_is_counted_without_failure();
        std::cout << "All verification tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
