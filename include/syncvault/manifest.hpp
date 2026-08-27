#pragma once

#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace syncvault {

enum class ManifestEntryType {
    regular_file,
    directory,
};

struct ManifestChunk {
    Sha256Digest digest;
    std::uint64_t size = 0U;
};

struct ManifestEntry {
    std::filesystem::path relative_path;
    ManifestEntryType type = ManifestEntryType::regular_file;
    std::uint64_t size = 0U;
    std::int64_t modified_time_ns = 0;
    std::vector<ManifestChunk> chunks;
};

struct SnapshotManifest {
    SnapshotSummary summary;
    std::filesystem::path source_root;
    std::vector<ManifestEntry> entries;
};

[[nodiscard]] SnapshotManifest read_snapshot_manifest(
    const std::filesystem::path& repository_root,
    const std::string& snapshot_id);

}  // namespace syncvault
