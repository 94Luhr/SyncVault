#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace syncvault {

inline constexpr std::uint16_t protocol_version = 1U;
inline constexpr std::size_t protocol_header_size = 48U;
inline constexpr std::uint64_t maximum_protocol_payload_size =
    16U * 1024U * 1024U;

enum class ProtocolMessageType : std::uint16_t {
    hello = 1U,
    hello_acknowledgement = 2U,
    chunk_offer = 3U,
    chunk_needed = 4U,
    chunk_data = 5U,
    manifest_offer = 6U,
    manifest_needed = 7U,
    manifest_data = 8U,
    complete = 9U,
    error = 10U,
};

struct ProtocolFrame {
    ProtocolMessageType type = ProtocolMessageType::error;
    std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::string_view to_string(ProtocolMessageType type) noexcept;

[[nodiscard]] std::vector<std::uint8_t> encode_protocol_frame(
    const ProtocolFrame& frame);

[[nodiscard]] ProtocolFrame decode_protocol_frame(
    std::span<const std::uint8_t> encoded);

}  // namespace syncvault
