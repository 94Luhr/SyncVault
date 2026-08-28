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

struct SyncPlan {
    std::uint64_t chunks_to_copy = 0U;
    std::uint64_t chunks_to_reuse = 0U;
    std::uint64_t snapshots_to_copy = 0U;
    std::uint64_t snapshots_to_reuse = 0U;
    std::uint64_t content_bytes_to_copy = 0U;
    std::uint64_t manifest_bytes_to_copy = 0U;
};

[[nodiscard]] SyncPlan plan_synchronization(
    const std::filesystem::path& source_repository,
    const std::filesystem::path& destination_repository);

[[nodiscard]] SyncResult synchronize_repositories(
    const std::filesystem::path& source_repository,
    const std::filesystem::path& destination_repository);

}  // namespace syncvault
