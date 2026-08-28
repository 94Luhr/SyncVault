#pragma once

#include <cstdint>
#include <filesystem>

namespace syncvault {

struct SyncResult {
    std::uint64_t chunks_copied = 0U;
    std::uint64_t chunks_reused = 0U;
    std::uint64_t snapshots_copied = 0U;
    std::uint64_t snapshots_reused = 0U;
    std::uint64_t content_bytes_copied = 0U;
    std::uint64_t manifest_bytes_copied = 0U;
};

[[nodiscard]] SyncResult synchronize_repositories(
    const std::filesystem::path& source_repository,
    const std::filesystem::path& destination_repository);

}  // namespace syncvault
