#pragma once

#include "syncvault/sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace syncvault {

inline constexpr std::size_t default_chunk_size = 4U * 1024U * 1024U;

struct ChunkDescriptor {
    std::uint64_t offset;
    std::uint64_t size;
    Sha256Digest digest;
};

[[nodiscard]] std::vector<ChunkDescriptor> chunk_file(
    const std::filesystem::path& path,
    std::size_t chunk_size = default_chunk_size);

}  // namespace syncvault