#include "syncvault/chunk_store.hpp"

#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace syncvault {
namespace {

std::atomic<std::uint64_t> temporary_sequence{0U};
std::array<std::mutex, 64U> chunk_mutexes;

std::mutex& chunk_mutex(const Sha256Digest& digest)
{
    return chunk_mutexes[digest.front() % chunk_mutexes.size()];
}

void verify_existing_chunk(const std::filesystem::path& path,
                           std::size_t expected_size,
                           const Sha256Digest& digest)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size != expected_size) {
        throw std::runtime_error("existing chunk failed integrity check");
    }

    std::vector<std::uint8_t> contents(expected_size);
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open existing chunk");
    }
    input.read(reinterpret_cast<char*>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size())
        || input.bad() || sha256(contents) != digest) {
        throw std::runtime_error("existing chunk failed integrity check");
    }
}

std::filesystem::path temporary_path(const std::filesystem::path& root,
                                     const std::string& digest)
{
    const auto ticks = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    const auto sequence = temporary_sequence.fetch_add(1U);
    return root / "tmp"
        / (digest + "." + std::to_string(ticks) + "."
           + std::to_string(sequence) + ".tmp");
}

bool store_chunk(const std::filesystem::path& root,
                 std::span<const std::uint8_t> contents,
                 const Sha256Digest& digest)
{
    const std::lock_guard lock(chunk_mutex(digest));
    const auto destination = chunk_path(root, digest);
    if (std::filesystem::exists(destination)) {
        verify_existing_chunk(destination, contents.size(), digest);
        return false;
    }

    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = temporary_path(root, to_hex(digest));

    try {
        {
            std::ofstream output(temporary,
                                 std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot create temporary chunk");
            }
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error("cannot write temporary chunk");
            }
        }

        std::error_code rename_error;
        std::filesystem::rename(temporary, destination, rename_error);
        if (!rename_error) {
            return true;
        }

        if (std::filesystem::exists(destination)) {
            verify_existing_chunk(destination, contents.size(), digest);
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        throw std::filesystem::filesystem_error(
            "cannot publish chunk", temporary, destination, rename_error);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

}  // namespace

std::filesystem::path chunk_path(const std::filesystem::path& repository_root,
                                 const Sha256Digest& digest)
{
    const auto hex = to_hex(digest);
    return repository_root / "chunks" / hex.substr(0U, 2U) / hex.substr(2U);
}

bool has_verified_chunk(const std::filesystem::path& repository_root,
                        const Sha256Digest& digest,
                        std::uint64_t expected_size)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    const std::lock_guard lock(chunk_mutex(digest));
    const auto path = chunk_path(repository_root, digest);
    if (!std::filesystem::exists(path)) {
        return false;
    }
    if (expected_size > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("chunk exceeds platform size limits");
    }
    verify_existing_chunk(path, static_cast<std::size_t>(expected_size), digest);
    return true;
}

bool store_verified_chunk(const std::filesystem::path& repository_root,
                          std::span<const std::uint8_t> contents,
                          const Sha256Digest& digest)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (sha256(contents) != digest) {
        throw std::runtime_error("chunk failed SHA-256 verification");
    }
    return store_chunk(repository_root, contents, digest);
}

StoreResult store_file_chunks(const std::filesystem::path& repository_root,
                              const std::filesystem::path& source,
                              std::size_t chunk_size)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (chunk_size == 0U) {
        throw std::invalid_argument("chunk size must be greater than zero");
    }
    if (chunk_size > static_cast<std::size_t>(
                         std::numeric_limits<std::streamsize>::max())) {
        throw std::invalid_argument("chunk size exceeds stream limits");
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        throw std::invalid_argument("chunk source must be a regular file");
    }
    const auto expected_size = std::filesystem::file_size(source, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read source file size", source, error);
    }
    const auto expected_write_time =
        std::filesystem::last_write_time(source, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read source last-write time", source, error);
    }

    std::ifstream input(source, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open source file for storage");
    }

    std::vector<std::uint8_t> buffer(chunk_size);
    StoreResult result;
    std::uint64_t offset = 0U;
    while (true) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = input.gcount();
        if (bytes_read <= 0) {
            break;
        }

        const auto size = static_cast<std::size_t>(bytes_read);
        const std::span<const std::uint8_t> contents(buffer.data(), size);
        const auto digest = sha256(contents);
        result.chunks.push_back({offset, static_cast<std::uint64_t>(size), digest});
        if (store_chunk(repository_root, contents, digest)) {
            ++result.chunks_written;
            result.bytes_written += static_cast<std::uint64_t>(size);
        } else {
            ++result.chunks_reused;
        }
        offset += static_cast<std::uint64_t>(size);
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading source file");
    }
    const auto final_size = std::filesystem::file_size(source, error);
    if (error || offset != expected_size || final_size != expected_size) {
        throw std::runtime_error("source file changed while it was being stored");
    }
    const auto final_write_time = std::filesystem::last_write_time(source, error);
    if (error || final_write_time != expected_write_time) {
        throw std::runtime_error("source file changed while it was being stored");
    }
    return result;
}

}  // namespace syncvault
