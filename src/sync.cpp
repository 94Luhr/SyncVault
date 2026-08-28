#include "syncvault/sync.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"
#include "syncvault/verify.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace syncvault {
namespace {

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path,
                                      std::uint64_t expected_size)
{
    if (expected_size
        > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
        || expected_size
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("file is too large to synchronize");
    }
    std::error_code error;
    const auto actual_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot access synchronization source", path, error);
    }
    if (actual_size != expected_size) {
        throw std::runtime_error("synchronization source size changed");
    }

    std::vector<std::uint8_t> contents(static_cast<std::size_t>(expected_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open synchronization source");
    }
    input.read(reinterpret_cast<char*>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size())
        || input.bad()) {
        throw std::runtime_error("cannot read synchronization source");
    }
    return contents;
}

std::vector<std::uint8_t> read_verified_chunk(
    const std::filesystem::path& path,
    const ManifestChunk& chunk)
{
    auto contents = read_binary(path, chunk.size);
    if (sha256(contents) != chunk.digest) {
        throw std::runtime_error(
            "synchronization chunk failed SHA-256 verification");
    }
    return contents;
}

std::filesystem::path temporary_path(const std::filesystem::path& repository,
                                     std::string_view label)
{
    const auto ticks = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    for (std::uint64_t sequence = 0U; sequence < 1'000U; ++sequence) {
        const auto candidate = repository / "tmp"
            / ("sync-" + std::string(label) + "-" + std::to_string(ticks)
               + "-" + std::to_string(sequence) + ".tmp");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("cannot allocate synchronization temporary file");
}

void publish_binary(const std::filesystem::path& repository,
                    const std::filesystem::path& destination,
                    std::span<const std::uint8_t> contents,
                    std::string_view label)
{
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = temporary_path(repository, label);
    try {
        {
            std::ofstream output(temporary,
                                 std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot create synchronization file");
            }
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error("cannot write synchronization file");
            }
        }
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

bool copy_chunk(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                const ManifestChunk& chunk)
{
    const auto source_path = chunk_path(source, chunk.digest);
    const auto destination_path = chunk_path(destination, chunk.digest);
    const auto contents = read_verified_chunk(source_path, chunk);
    if (std::filesystem::exists(destination_path)) {
        static_cast<void>(read_verified_chunk(destination_path, chunk));
        return false;
    }

    publish_binary(destination,
                   destination_path,
                   contents,
                   to_hex(chunk.digest).substr(0U, 12U));
    return true;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("cannot read snapshot manifest size");
    }
    return read_binary(path, static_cast<std::uint64_t>(size));
}

bool copy_manifest(const std::filesystem::path& source,
                   const std::filesystem::path& destination,
                   const std::string& id,
                   std::uint64_t& bytes_copied)
{
    const auto filename = id + ".svsnap";
    const auto source_path = source / "snapshots" / filename;
    const auto destination_path = destination / "snapshots" / filename;
    const auto contents = read_file(source_path);
    if (std::filesystem::exists(destination_path)) {
        if (read_file(destination_path) != contents) {
            throw std::runtime_error(
                "destination contains a conflicting snapshot manifest");
        }
        return false;
    }
    publish_binary(destination, destination_path, contents, id.substr(0U, 12U));
    bytes_copied += static_cast<std::uint64_t>(contents.size());
    return true;
}

}  // namespace

SyncResult synchronize_repositories(
    const std::filesystem::path& source_repository,
    const std::filesystem::path& destination_repository)
{
    const auto source =
        std::filesystem::absolute(source_repository).lexically_normal();
    const auto destination =
        std::filesystem::absolute(destination_repository).lexically_normal();
    if (!Repository::is_repository(source)
        || !Repository::is_repository(destination)) {
        throw std::invalid_argument(
            "source and destination must be SyncVault repositories");
    }
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(source, destination, equivalent_error)
        && !equivalent_error) {
        throw std::invalid_argument(
            "source and destination repositories must be different");
    }

    const auto verification = verify_repository(source);
    if (!verification.healthy()) {
        throw std::runtime_error(
            "source repository failed integrity verification");
    }

    auto summaries = list_snapshots(source);
    std::reverse(summaries.begin(), summaries.end());
    std::vector<SnapshotManifest> manifests;
    std::map<std::string, ManifestChunk> chunks;
    for (const auto& summary : summaries) {
        auto manifest = read_snapshot_manifest(source, summary.id);
        for (const auto& entry : manifest.entries) {
            for (const auto& chunk : entry.chunks) {
                chunks.emplace(to_hex(chunk.digest), chunk);
            }
        }
        manifests.push_back(std::move(manifest));
    }

    SyncResult result;
    for (const auto& [id, chunk] : chunks) {
        static_cast<void>(id);
        if (copy_chunk(source, destination, chunk)) {
            ++result.chunks_copied;
            result.content_bytes_copied += chunk.size;
        } else {
            ++result.chunks_reused;
        }
    }
    for (const auto& manifest : manifests) {
        if (copy_manifest(source,
                          destination,
                          manifest.summary.id,
                          result.manifest_bytes_copied)) {
            ++result.snapshots_copied;
        } else {
            ++result.snapshots_reused;
        }
    }
    return result;
}

}  // namespace syncvault
