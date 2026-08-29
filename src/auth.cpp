#include "syncvault/auth.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace syncvault {

Sha256Digest hmac_sha256(std::string_view secret,
                         std::span<const std::uint8_t> message)
{
    constexpr std::size_t block_size = 64U;
    std::array<std::uint8_t, block_size> key{};
    if (secret.size() > block_size) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(secret.data());
        const auto digest = sha256({bytes, secret.size()});
        std::copy(digest.begin(), digest.end(), key.begin());
    } else {
        std::copy(secret.begin(), secret.end(), key.begin());
    }

    std::array<std::uint8_t, block_size> inner_pad{};
    std::array<std::uint8_t, block_size> outer_pad{};
    for (std::size_t index = 0U; index < block_size; ++index) {
        inner_pad[index] = static_cast<std::uint8_t>(key[index] ^ 0x36U);
        outer_pad[index] = static_cast<std::uint8_t>(key[index] ^ 0x5cU);
    }

    std::vector<std::uint8_t> inner(inner_pad.begin(), inner_pad.end());
    inner.insert(inner.end(), message.begin(), message.end());
    const auto inner_digest = sha256(inner);
    std::vector<std::uint8_t> outer(outer_pad.begin(), outer_pad.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return sha256(outer);
}

bool constant_time_equal(std::span<const std::uint8_t> left,
                         std::span<const std::uint8_t> right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    std::uint8_t difference = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | (left[index] ^ right[index]));
    }
    return difference == 0U;
}

}  // namespace syncvault
