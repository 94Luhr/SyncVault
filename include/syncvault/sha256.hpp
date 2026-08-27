#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace syncvault {

using Sha256Digest = std::array<std::uint8_t, 32>;

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> input);

[[nodiscard]] std::string to_hex(const Sha256Digest& digest);

}  // namespace syncvault