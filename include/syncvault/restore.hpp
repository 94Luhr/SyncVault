#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace syncvault {

struct RestoreResult {
    std::uint64_t files_restored = 0U;
    std::uint64_t directories_restored = 0U;
    std::uint64_t bytes_restored = 0U;
};

[[nodiscard]] RestoreResult restore_snapshot(
    const std::filesystem::path& repository_root,
    const std::string& snapshot_id,
    const std::filesystem::path& destination_root);

}  // namespace syncvault
