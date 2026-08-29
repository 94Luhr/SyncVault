#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace syncvault {

struct NetworkChunkSyncResult {
    std::uint64_t chunks_transferred = 0U;
    std::uint64_t chunks_reused = 0U;
    std::uint64_t bytes_transferred = 0U;
    std::string peer;
};

struct HandshakeResult {
    std::uint16_t protocol_version = 0U;
    std::string peer;
};

class ProtocolHandshakeServer {
public:
    explicit ProtocolHandshakeServer(
        const std::filesystem::path& repository,
        std::uint16_t port);
    ~ProtocolHandshakeServer();

    ProtocolHandshakeServer(const ProtocolHandshakeServer&) = delete;
    ProtocolHandshakeServer& operator=(const ProtocolHandshakeServer&) = delete;

    [[nodiscard]] std::uint16_t local_port() const noexcept;
    [[nodiscard]] HandshakeResult accept_once();
    [[nodiscard]] NetworkChunkSyncResult accept_chunk_sync_once();

private:
    std::filesystem::path repository_;
    std::uintptr_t listener_ = static_cast<std::uintptr_t>(-1);
    std::uint16_t local_port_ = 0U;
    bool socket_runtime_initialized_ = false;
};

[[nodiscard]] NetworkChunkSyncResult push_repository_chunks(
    const std::filesystem::path& source_repository,
    const std::string& host,
    std::uint16_t port);

[[nodiscard]] HandshakeResult perform_protocol_handshake(
    const std::string& host,
    std::uint16_t port);

}  // namespace syncvault
