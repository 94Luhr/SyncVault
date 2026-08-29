#pragma once

#include "syncvault/chunker.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace syncvault {

struct StoreResult {
    std::vector<ChunkDescriptor> chunks;
    std::size_t chunks_written = 0U;
    std::size_t chunks_reused = 0U;
    std::uint64_t bytes_written = 0U;
};

[[nodiscard]] std::filesystem::path chunk_path(
    const std::filesystem::path& repository_root,
    const Sha256Digest& digest);

[[nodiscard]] bool has_verified_chunk(
    const std::filesystem::path& repository_root,
    const Sha256Digest& digest,
    std::uint64_t expected_size);

[[nodiscard]] bool store_verified_chunk(
    const std::filesystem::path& repository_root,
    std::span<const std::uint8_t> contents,
    const Sha256Digest& digest);

[[nodiscard]] StoreResult store_file_chunks(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& source,
    std::size_t chunk_size = default_chunk_size);

}  // namespace syncvault
