#pragma once

#include "syncvault/sha256.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace syncvault {

[[nodiscard]] Sha256Digest hmac_sha256(
    std::string_view secret,
    std::span<const std::uint8_t> message);

[[nodiscard]] bool constant_time_equal(
    std::span<const std::uint8_t> left,
    std::span<const std::uint8_t> right) noexcept;

}  // namespace syncvault
