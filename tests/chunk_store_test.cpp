#include "syncvault/chunk_store.hpp"
#include "syncvault/repository.hpp"

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
            / ("syncvault-store-test-" + std::to_string(unique));
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

void chunks_are_stored_by_digest_and_reused()
{
    TemporaryDirectory temporary;
    const auto repository_root = temporary.path() / "repository";
    syncvault::Repository::initialize(repository_root);
    const auto source = temporary.path() / "data.bin";
    std::ofstream(source, std::ios::binary) << "abcdefghij";

    const auto first = syncvault::store_file_chunks(repository_root, source, 4U);
    require(first.chunks.size() == 3U, "expected three chunk descriptors");
    require(first.chunks_written == 3U && first.chunks_reused == 0U,
            "first storage should write all chunks");
    require(first.bytes_written == 10U, "written byte count is incorrect");

    const auto first_path = syncvault::chunk_path(
        repository_root, first.chunks.front().digest);
    require(first_path.parent_path().filename().string() == "88",
            "chunk path should use a two-character digest prefix");
    require(read_text(first_path) == "abcd", "stored chunk content is incorrect");

    const auto second = syncvault::store_file_chunks(repository_root, source, 4U);
    require(second.chunks_written == 0U && second.chunks_reused == 3U,
            "second storage should reuse all chunks");
    require(second.bytes_written == 0U,
            "reused chunks should not count as written bytes");
    require(std::filesystem::is_empty(repository_root / "tmp"),
            "temporary files should be cleaned up");
}

void corrupted_existing_chunk_is_rejected()
{
    TemporaryDirectory temporary;
    const auto repository_root = temporary.path() / "repository";
    syncvault::Repository::initialize(repository_root);
    const auto source = temporary.path() / "data.bin";
    std::ofstream(source, std::ios::binary) << "abcd";
    const auto stored = syncvault::store_file_chunks(repository_root, source, 4U);
    const auto stored_path = syncvault::chunk_path(
        repository_root, stored.chunks.front().digest);
    std::ofstream(stored_path, std::ios::binary | std::ios::trunc) << "wxyz";

    bool failed = false;
    try {
        static_cast<void>(
            syncvault::store_file_chunks(repository_root, source, 4U));
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed, "corrupted existing chunks should be rejected");
}

void non_repository_is_rejected()
{
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "data.bin";
    std::ofstream(source, std::ios::binary) << "data";

    bool failed = false;
    try {
        static_cast<void>(
            syncvault::store_file_chunks(temporary.path(), source, 4U));
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed, "storage outside a repository should be rejected");
}

}  // namespace

int main()
{
    try {
        chunks_are_stored_by_digest_and_reused();
        corrupted_existing_chunk_is_rejected();
        non_repository_is_rejected();
        std::cout << "All chunk store tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
