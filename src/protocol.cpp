#include "syncvault/protocol.hpp"

#include "syncvault/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace syncvault {
namespace {

constexpr std::array<std::uint8_t, 4> magic{'S', 'V', 'N', 'P'};

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value)
{
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> input,
                       std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8U)
        | static_cast<std::uint16_t>(input[offset + 1U]));
}

std::uint64_t read_u64(std::span<const std::uint8_t> input,
                       std::size_t offset)
{
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) | input[offset + index];
    }
    return value;
}

bool is_known_type(std::uint16_t value) noexcept
{
    return value >= static_cast<std::uint16_t>(ProtocolMessageType::hello)
        && value <= static_cast<std::uint16_t>(ProtocolMessageType::error);
}

}  // namespace

std::string_view to_string(ProtocolMessageType type) noexcept
{
    switch (type) {
    case ProtocolMessageType::hello:
        return "hello";
    case ProtocolMessageType::hello_acknowledgement:
        return "hello-acknowledgement";
    case ProtocolMessageType::chunk_offer:
        return "chunk-offer";
    case ProtocolMessageType::chunk_needed:
        return "chunk-needed";
    case ProtocolMessageType::chunk_data:
        return "chunk-data";
    case ProtocolMessageType::manifest_offer:
        return "manifest-offer";
    case ProtocolMessageType::manifest_needed:
        return "manifest-needed";
    case ProtocolMessageType::manifest_data:
        return "manifest-data";
    case ProtocolMessageType::complete:
        return "complete";
    case ProtocolMessageType::error:
        return "error";
    }
    return "unknown";
}

std::vector<std::uint8_t> encode_protocol_frame(const ProtocolFrame& frame)
{
    if (frame.payload.size() > maximum_protocol_payload_size) {
        throw std::length_error("protocol payload exceeds the size limit");
    }
    const auto raw_type = static_cast<std::uint16_t>(frame.type);
    if (!is_known_type(raw_type)) {
        throw std::invalid_argument("unknown protocol message type");
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(protocol_header_size + frame.payload.size());
    encoded.insert(encoded.end(), magic.begin(), magic.end());
    append_u16(encoded, protocol_version);
    append_u16(encoded, raw_type);
    append_u64(encoded, static_cast<std::uint64_t>(frame.payload.size()));
    const auto digest = sha256(frame.payload);
    encoded.insert(encoded.end(), digest.begin(), digest.end());
    encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
    return encoded;
}

ProtocolFrame decode_protocol_frame(std::span<const std::uint8_t> encoded)
{
    if (encoded.size() < protocol_header_size) {
        throw std::invalid_argument("truncated protocol frame header");
    }
    if (!std::equal(magic.begin(), magic.end(), encoded.begin())) {
        throw std::invalid_argument("invalid protocol frame magic");
    }
    if (read_u16(encoded, 4U) != protocol_version) {
        throw std::invalid_argument("unsupported protocol version");
    }
    const auto raw_type = read_u16(encoded, 6U);
    if (!is_known_type(raw_type)) {
        throw std::invalid_argument("unknown protocol message type");
    }
    const auto payload_size = read_u64(encoded, 8U);
    if (payload_size > maximum_protocol_payload_size
        || payload_size > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("protocol payload exceeds the size limit");
    }
    if (encoded.size() - protocol_header_size
        != static_cast<std::size_t>(payload_size)) {
        throw std::invalid_argument("protocol frame length mismatch");
    }

    Sha256Digest expected_digest{};
    std::copy_n(encoded.begin() + 16U,
                expected_digest.size(),
                expected_digest.begin());
    const auto payload = encoded.subspan(protocol_header_size);
    if (sha256(payload) != expected_digest) {
        throw std::runtime_error("protocol payload failed SHA-256 verification");
    }
    return {
        static_cast<ProtocolMessageType>(raw_type),
        std::vector<std::uint8_t>(payload.begin(), payload.end()),
    };
}

}  // namespace syncvault
