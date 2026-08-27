#include "syncvault/restore.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace syncvault {
namespace {

bool path_is_within(const std::filesystem::path& child,
                    const std::filesystem::path& parent)
{
    const auto relative = child.lexically_relative(parent);
    if (relative.empty()) {
        return child == parent;
    }
    return *relative.begin() != "..";
}

std::filesystem::file_time_type file_time_from_unix_ns(std::int64_t value)
{
#ifdef _MSC_VER
    using FiletimeDuration =
        std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    constexpr std::int64_t windows_to_unix_epoch_ticks =
        116'444'736'000'000'000;
    const auto ticks = value / 100 + windows_to_unix_epoch_ticks;
    return std::filesystem::file_time_type(
        std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
            FiletimeDuration(ticks)));
#else
    const auto system_time = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(value));
    const auto file_time = std::filesystem::file_time_type::clock::now()
        + (system_time - std::chrono::system_clock::now());
    return std::chrono::time_point_cast<
        std::filesystem::file_time_type::duration>(file_time);
#endif
}

void set_modified_time(const std::filesystem::path& path,
                       std::int64_t modified_time_ns)
{
    std::error_code error;
    std::filesystem::last_write_time(
        path, file_time_from_unix_ns(modified_time_ns), error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot restore last-write time", path, error);
    }
}

std::filesystem::path staging_path_for(
    const std::filesystem::path& destination)
{
    const auto base = destination.filename().string()
        + ".syncvault-restore-"
        + std::to_string(std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count());
    for (std::uint64_t sequence = 0U; sequence < 1'000U; ++sequence) {
        const auto candidate = destination.parent_path()
            / (base + "-" + std::to_string(sequence) + ".tmp");
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("cannot allocate restore staging directory");
}

std::vector<std::uint8_t> read_and_verify_chunk(
    const std::filesystem::path& repository,
    const ManifestChunk& reference)
{
    if (reference.size
        > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())
        || reference.size
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("snapshot chunk is too large to restore");
    }
    const auto path = chunk_path(repository, reference.digest);
    std::error_code error;
    const auto actual_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot access snapshot chunk", path, error);
    }
    if (actual_size != reference.size) {
        throw std::runtime_error("snapshot chunk size does not match manifest");
    }

    std::vector<std::uint8_t> contents(static_cast<std::size_t>(reference.size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open snapshot chunk");
    }
    input.read(reinterpret_cast<char*>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size())
        || input.bad() || sha256(contents) != reference.digest) {
        throw std::runtime_error("snapshot chunk failed integrity check");
    }
    return contents;
}

void restore_file(const std::filesystem::path& repository,
                  const std::filesystem::path& destination,
                  const ManifestEntry& entry)
{
    std::filesystem::create_directories(destination.parent_path());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create restored file");
    }

    std::uint64_t bytes_written = 0U;
    for (const auto& chunk : entry.chunks) {
        const auto contents = read_and_verify_chunk(repository, chunk);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
        if (!output) {
            throw std::runtime_error("cannot write restored file");
        }
        bytes_written += static_cast<std::uint64_t>(contents.size());
    }
    output.flush();
    if (!output || bytes_written != entry.size) {
        throw std::runtime_error("restored file size does not match manifest");
    }
    output.close();
    set_modified_time(destination, entry.modified_time_ns);
}

std::size_t path_depth(const std::filesystem::path& path)
{
    return static_cast<std::size_t>(std::distance(path.begin(), path.end()));
}

}  // namespace

RestoreResult restore_snapshot(const std::filesystem::path& repository_root,
                               const std::string& snapshot_id,
                               const std::filesystem::path& destination_root)
{
    const auto repository =
        std::filesystem::absolute(repository_root).lexically_normal();
    const auto destination =
        std::filesystem::absolute(destination_root).lexically_normal();
    if (!Repository::is_repository(repository)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (destination_root.empty()) {
        throw std::invalid_argument("restore destination must not be empty");
    }
    if (path_is_within(destination, repository)) {
        throw std::invalid_argument(
            "restore destination must not be inside the repository");
    }

    const bool destination_exists = std::filesystem::exists(destination);
    if (destination_exists
        && (!std::filesystem::is_directory(destination)
            || !std::filesystem::is_empty(destination))) {
        throw std::invalid_argument(
            "restore destination must be absent or an empty directory");
    }

    const auto manifest = read_snapshot_manifest(repository, snapshot_id);
    std::filesystem::create_directories(destination.parent_path());
    const auto staging = staging_path_for(destination);
    std::filesystem::create_directory(staging);

    RestoreResult result;
    std::vector<const ManifestEntry*> directories;
    try {
        for (const auto& entry : manifest.entries) {
            const auto target = staging / entry.relative_path;
            if (entry.type == ManifestEntryType::directory) {
                std::filesystem::create_directories(target);
                directories.push_back(&entry);
                ++result.directories_restored;
            } else {
                restore_file(repository, target, entry);
                ++result.files_restored;
                result.bytes_restored += entry.size;
            }
        }

        std::sort(directories.begin(), directories.end(),
                  [](const auto* left, const auto* right) {
                      return path_depth(left->relative_path)
                          > path_depth(right->relative_path);
                  });
        for (const auto* entry : directories) {
            set_modified_time(staging / entry->relative_path,
                              entry->modified_time_ns);
        }

        if (destination_exists) {
            std::filesystem::remove(destination);
        }
        std::error_code rename_error;
        std::filesystem::rename(staging, destination, rename_error);
        if (rename_error) {
            if (destination_exists) {
                std::error_code ignored;
                std::filesystem::create_directory(destination, ignored);
            }
            throw std::filesystem::filesystem_error(
                "cannot publish restored snapshot",
                staging,
                destination,
                rename_error);
        }
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(staging, ignored);
        throw;
    }
    return result;
}

}  // namespace syncvault
