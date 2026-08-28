#include "syncvault/protocol.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Exception, typename Operation>
void require_throws(Operation operation, const std::string& message)
{
    try {
        operation();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void frames_round_trip_binary_payloads()
{
    const syncvault::ProtocolFrame original{
        syncvault::ProtocolMessageType::chunk_data,
        {0U, 1U, 2U, 0U, 255U, 128U},
    };
    const auto encoded = syncvault::encode_protocol_frame(original);
    require(encoded.size()
                == syncvault::protocol_header_size + original.payload.size(),
            "encoded frame length is incorrect");
    const auto decoded = syncvault::decode_protocol_frame(encoded);
    require(decoded.type == original.type,
            "decoded protocol message type is incorrect");
    require(decoded.payload == original.payload,
            "binary protocol payload did not round trip");
}

void malformed_frames_are_rejected()
{
    const syncvault::ProtocolFrame original{
        syncvault::ProtocolMessageType::manifest_data,
        {'m', 'a', 'n', 'i', 'f', 'e', 's', 't'},
    };
    const auto valid = syncvault::encode_protocol_frame(original);

    auto corrupt_payload = valid;
    corrupt_payload.back() ^= 0x80U;
    require_throws<std::runtime_error>(
        [&] { static_cast<void>(
            syncvault::decode_protocol_frame(corrupt_payload)); },
        "corrupted payload should fail integrity verification");

    auto wrong_version = valid;
    wrong_version[5U] = 2U;
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(
            syncvault::decode_protocol_frame(wrong_version)); },
        "unsupported protocol version should be rejected");

    auto unknown_type = valid;
    unknown_type[6U] = 0U;
    unknown_type[7U] = 99U;
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(
            syncvault::decode_protocol_frame(unknown_type)); },
        "unknown message type should be rejected");

    auto truncated = valid;
    truncated.pop_back();
    require_throws<std::invalid_argument>(
        [&] { static_cast<void>(
            syncvault::decode_protocol_frame(truncated)); },
        "truncated frame should be rejected");
}

void empty_payloads_are_supported()
{
    const syncvault::ProtocolFrame original{
        syncvault::ProtocolMessageType::complete,
        {},
    };
    const auto decoded = syncvault::decode_protocol_frame(
        syncvault::encode_protocol_frame(original));
    require(decoded.type == syncvault::ProtocolMessageType::complete
                && decoded.payload.empty(),
            "empty protocol payload did not round trip");
    require(syncvault::to_string(decoded.type) == "complete",
            "protocol type name is incorrect");
}

}  // namespace

int main()
{
    try {
        frames_round_trip_binary_payloads();
        malformed_frames_are_rejected();
        empty_payloads_are_supported();
        std::cout << "All protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
