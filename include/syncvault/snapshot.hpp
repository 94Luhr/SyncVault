#pragma once

#include "syncvault/chunker.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace syncvault {

struct SnapshotSummary {
    std::string id;
    std::int64_t created_at_ns = 0;
    std::uint64_t file_count = 0U;
    std::uint64_t directory_count = 0U;
    std::uint64_t total_bytes = 0U;
};

struct SnapshotResult {
    SnapshotSummary snapshot;
    std::size_t chunks_written = 0U;
    std::size_t chunks_reused = 0U;
    std::uint64_t bytes_written = 0U;
};

[[nodiscard]] SnapshotResult create_snapshot(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& source_root,
    std::size_t chunk_size = default_chunk_size);

[[nodiscard]] std::vector<SnapshotSummary> list_snapshots(
    const std::filesystem::path& repository_root);

}  // namespace syncvault
