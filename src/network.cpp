#include "syncvault/network.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/protocol.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"
#include "syncvault/snapshot.hpp"
#include "syncvault/verify.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace syncvault {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr NativeSocket invalid_socket = -1;
#endif

NativeSocket from_storage(std::uintptr_t value) noexcept
{
    return static_cast<NativeSocket>(value);
}

std::uintptr_t to_storage(NativeSocket value) noexcept
{
    return static_cast<std::uintptr_t>(value);
}

void initialize_socket_runtime()
{
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("cannot initialize Winsock");
    }
#endif
}

void cleanup_socket_runtime() noexcept
{
#ifdef _WIN32
    WSACleanup();
#endif
}

void close_socket(NativeSocket socket) noexcept
{
    if (socket == invalid_socket) {
        return;
    }
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

[[noreturn]] void throw_socket_error(const std::string& operation)
{
#ifdef _WIN32
    throw std::runtime_error(
        operation + " failed with socket error "
        + std::to_string(WSAGetLastError()));
#else
    throw std::runtime_error(operation + " failed: " + std::strerror(errno));
#endif
}

void configure_timeouts(NativeSocket socket)
{
#ifdef _WIN32
    constexpr DWORD timeout_ms = 5'000U;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms),
                   sizeof(timeout_ms)) != 0
        || setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                      reinterpret_cast<const char*>(&timeout_ms),
                      sizeof(timeout_ms)) != 0) {
        throw_socket_error("setting socket timeout");
    }
#else
    constexpr timeval timeout{5, 0};
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) != 0
        || setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                      sizeof(timeout)) != 0) {
        throw_socket_error("setting socket timeout");
    }
#endif
}

void send_all(NativeSocket socket, std::span<const std::uint8_t> bytes)
{
    std::size_t sent = 0U;
    while (sent < bytes.size()) {
        const auto remaining = bytes.size() - sent;
        const auto batch = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto result = send(
            socket,
            reinterpret_cast<const char*>(bytes.data() + sent),
            batch,
            0);
        if (result <= 0) {
            throw_socket_error("sending protocol frame");
        }
        sent += static_cast<std::size_t>(result);
    }
}

void receive_exact(NativeSocket socket, std::span<std::uint8_t> bytes)
{
    std::size_t received = 0U;
    while (received < bytes.size()) {
        const auto remaining = bytes.size() - received;
        const auto batch = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto result = recv(
            socket,
            reinterpret_cast<char*>(bytes.data() + received),
            batch,
            0);
        if (result == 0) {
            throw std::runtime_error("peer closed the protocol connection");
        }
        if (result < 0) {
            throw_socket_error("receiving protocol frame");
        }
        received += static_cast<std::size_t>(result);
    }
}

std::uint64_t payload_size_from_header(
    std::span<const std::uint8_t> header) noexcept
{
    std::uint64_t value = 0U;
    for (std::size_t index = 8U; index < 16U; ++index) {
        value = (value << 8U) | header[index];
    }
    return value;
}

void send_frame(NativeSocket socket, const ProtocolFrame& frame)
{
    const auto encoded = encode_protocol_frame(frame);
    send_all(socket, encoded);
}

ProtocolFrame receive_frame(NativeSocket socket)
{
    std::array<std::uint8_t, protocol_header_size> header{};
    receive_exact(socket, header);
    const auto payload_size = payload_size_from_header(header);
    if (payload_size > maximum_protocol_payload_size) {
        throw std::length_error("incoming protocol payload exceeds the size limit");
    }
    std::vector<std::uint8_t> encoded(
        protocol_header_size + static_cast<std::size_t>(payload_size));
    std::copy(header.begin(), header.end(), encoded.begin());
    receive_exact(socket,
                  std::span<std::uint8_t>(encoded).subspan(protocol_header_size));
    return decode_protocol_frame(encoded);
}

std::vector<std::uint8_t> hello_payload()
{
    return {
        static_cast<std::uint8_t>(protocol_version >> 8U),
        static_cast<std::uint8_t>(protocol_version),
    };
}

std::uint16_t parse_hello(const ProtocolFrame& frame,
                          ProtocolMessageType expected_type)
{
    if (frame.type != expected_type || frame.payload.size() != 2U) {
        throw std::runtime_error("invalid protocol handshake message");
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(frame.payload[0]) << 8U)
        | frame.payload[1]);
}

std::string numeric_peer_name(const sockaddr* address, SocketLength length)
{
    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    if (getnameinfo(address, length,
                    host.data(), static_cast<SocketLength>(host.size()),
                    service.data(), static_cast<SocketLength>(service.size()),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        return "unknown";
    }
    return std::string(host.data()) + ":" + service.data();
}

NativeSocket connect_to(const std::string& host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
        throw std::runtime_error("cannot resolve protocol server host");
    }

    NativeSocket connected = invalid_socket;
    for (auto* address = addresses; address != nullptr;
         address = address->ai_next) {
        const auto candidate = socket(address->ai_family,
                                      address->ai_socktype,
                                      address->ai_protocol);
        if (candidate == invalid_socket) {
            continue;
        }
        if (connect(candidate, address->ai_addr,
                    static_cast<int>(address->ai_addrlen)) == 0) {
            connected = candidate;
            break;
        }
        close_socket(candidate);
    }
    freeaddrinfo(addresses);
    if (connected == invalid_socket) {
        throw_socket_error("connecting to protocol server");
    }
    return connected;
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
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

ProtocolFrame make_chunk_offer(const ManifestChunk& chunk)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(40U);
    payload.insert(payload.end(), chunk.digest.begin(), chunk.digest.end());
    append_u64(payload, chunk.size);
    return {ProtocolMessageType::chunk_offer, std::move(payload)};
}

ManifestChunk parse_chunk_offer(const ProtocolFrame& frame)
{
    if (frame.type != ProtocolMessageType::chunk_offer
        || frame.payload.size() != 40U) {
        throw std::runtime_error("invalid chunk offer");
    }
    ManifestChunk chunk;
    std::copy_n(frame.payload.begin(), chunk.digest.size(), chunk.digest.begin());
    chunk.size = read_u64(frame.payload, 32U);
    if (chunk.size > maximum_protocol_payload_size - chunk.digest.size()) {
        throw std::length_error("offered chunk exceeds protocol frame limit");
    }
    return chunk;
}

std::map<std::string, ManifestChunk> referenced_chunks(
    const std::filesystem::path& repository)
{
    if (!verify_repository(repository).healthy()) {
        throw std::runtime_error("source repository failed integrity verification");
    }
    std::map<std::string, ManifestChunk> chunks;
    for (const auto& summary : list_snapshots(repository)) {
        const auto manifest = read_snapshot_manifest(repository, summary.id);
        for (const auto& entry : manifest.entries) {
            for (const auto& chunk : entry.chunks) {
                chunks.emplace(to_hex(chunk.digest), chunk);
            }
        }
    }
    return chunks;
}

std::vector<std::uint8_t> read_chunk_contents(
    const std::filesystem::path& repository,
    const ManifestChunk& chunk)
{
    if (chunk.size > std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("chunk exceeds platform size limits");
    }
    const auto path = chunk_path(repository, chunk.digest);
    std::error_code error;
    if (std::filesystem::file_size(path, error) != chunk.size || error) {
        throw std::runtime_error("source chunk size changed during network sync");
    }
    std::vector<std::uint8_t> contents(static_cast<std::size_t>(chunk.size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open source chunk");
    }
    input.read(reinterpret_cast<char*>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size())
        || input.bad() || sha256(contents) != chunk.digest) {
        throw std::runtime_error("source chunk failed integrity verification");
    }
    return contents;
}

std::uint16_t exchange_client_hello(NativeSocket socket)
{
    send_frame(socket, {ProtocolMessageType::hello, hello_payload()});
    const auto version = parse_hello(
        receive_frame(socket), ProtocolMessageType::hello_acknowledgement);
    if (version != protocol_version) {
        throw std::runtime_error("server selected an unsupported protocol version");
    }
    return version;
}

std::uint16_t exchange_server_hello(NativeSocket socket)
{
    const auto version = parse_hello(
        receive_frame(socket), ProtocolMessageType::hello);
    if (version != protocol_version) {
        throw std::runtime_error("peer requested an unsupported protocol version");
    }
    send_frame(socket, {
        ProtocolMessageType::hello_acknowledgement,
        hello_payload(),
    });
    return version;
}

}  // namespace

ProtocolHandshakeServer::ProtocolHandshakeServer(
    const std::filesystem::path& repository,
    std::uint16_t port)
{
    repository_ = std::filesystem::absolute(repository).lexically_normal();
    if (!Repository::is_repository(repository_)) {
        throw std::invalid_argument("server path is not a SyncVault repository");
    }
    initialize_socket_runtime();
    socket_runtime_initialized_ = true;
    NativeSocket listener = invalid_socket;
    try {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == invalid_socket) {
            throw_socket_error("creating protocol listener");
        }
        int reuse = 1;
        static_cast<void>(setsockopt(
            listener, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&reuse), sizeof(reuse)));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (bind(listener, reinterpret_cast<const sockaddr*>(&address),
                 sizeof(address)) != 0) {
            throw_socket_error("binding protocol listener");
        }
        if (listen(listener, 8) != 0) {
            throw_socket_error("starting protocol listener");
        }
        sockaddr_in bound{};
        SocketLength bound_length = sizeof(bound);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&bound),
                        &bound_length) != 0) {
            throw_socket_error("reading protocol listener address");
        }
        listener_ = to_storage(listener);
        local_port_ = ntohs(bound.sin_port);
    } catch (...) {
        close_socket(listener);
        cleanup_socket_runtime();
        socket_runtime_initialized_ = false;
        throw;
    }
}

ProtocolHandshakeServer::~ProtocolHandshakeServer()
{
    close_socket(from_storage(listener_));
    if (socket_runtime_initialized_) {
        cleanup_socket_runtime();
    }
}

std::uint16_t ProtocolHandshakeServer::local_port() const noexcept
{
    return local_port_;
}

HandshakeResult ProtocolHandshakeServer::accept_once()
{
    sockaddr_storage peer_address{};
    SocketLength peer_length = sizeof(peer_address);
    const auto peer = accept(from_storage(listener_),
                             reinterpret_cast<sockaddr*>(&peer_address),
                             &peer_length);
    if (peer == invalid_socket) {
        throw_socket_error("accepting protocol connection");
    }
    try {
        configure_timeouts(peer);
        const auto version = exchange_server_hello(peer);
        const auto name = numeric_peer_name(
            reinterpret_cast<const sockaddr*>(&peer_address), peer_length);
        close_socket(peer);
        return {version, name};
    } catch (...) {
        close_socket(peer);
        throw;
    }
}

NetworkChunkSyncResult ProtocolHandshakeServer::accept_chunk_sync_once()
{
    sockaddr_storage peer_address{};
    SocketLength peer_length = sizeof(peer_address);
    const auto peer = accept(from_storage(listener_),
                             reinterpret_cast<sockaddr*>(&peer_address),
                             &peer_length);
    if (peer == invalid_socket) {
        throw_socket_error("accepting chunk synchronization connection");
    }
    try {
        configure_timeouts(peer);
        static_cast<void>(exchange_server_hello(peer));
        NetworkChunkSyncResult result;
        result.peer = numeric_peer_name(
            reinterpret_cast<const sockaddr*>(&peer_address), peer_length);
        while (true) {
            const auto frame = receive_frame(peer);
            if (frame.type == ProtocolMessageType::complete) {
                if (!frame.payload.empty()) {
                    throw std::runtime_error("invalid synchronization completion");
                }
                send_frame(peer, {ProtocolMessageType::complete, {}});
                close_socket(peer);
                return result;
            }
            const auto chunk = parse_chunk_offer(frame);
            const auto needed =
                !has_verified_chunk(repository_, chunk.digest, chunk.size);
            send_frame(peer, {
                ProtocolMessageType::chunk_needed,
                {static_cast<std::uint8_t>(needed ? 1U : 0U)},
            });
            if (!needed) {
                ++result.chunks_reused;
                continue;
            }

            const auto data = receive_frame(peer);
            if (data.type != ProtocolMessageType::chunk_data
                || data.payload.size() != chunk.size + chunk.digest.size()
                || !std::equal(chunk.digest.begin(), chunk.digest.end(),
                               data.payload.begin())) {
                throw std::runtime_error("invalid chunk data message");
            }
            const auto contents = std::span<const std::uint8_t>(data.payload)
                                      .subspan(chunk.digest.size());
            if (!store_verified_chunk(repository_, contents, chunk.digest)) {
                throw std::runtime_error("chunk appeared during network transfer");
            }
            ++result.chunks_transferred;
            result.bytes_transferred += chunk.size;
        }
    } catch (...) {
        close_socket(peer);
        throw;
    }
}

NetworkChunkSyncResult push_repository_chunks(
    const std::filesystem::path& source_repository,
    const std::string& host,
    std::uint16_t port)
{
    if (host.empty() || port == 0U) {
        throw std::invalid_argument("protocol server host and port are required");
    }
    const auto source =
        std::filesystem::absolute(source_repository).lexically_normal();
    if (!Repository::is_repository(source)) {
        throw std::invalid_argument("source path is not a SyncVault repository");
    }
    const auto chunks = referenced_chunks(source);
    initialize_socket_runtime();
    NativeSocket socket = invalid_socket;
    try {
        socket = connect_to(host, port);
        configure_timeouts(socket);
        static_cast<void>(exchange_client_hello(socket));
        NetworkChunkSyncResult result;
        result.peer = host + ":" + std::to_string(port);
        for (const auto& [id, chunk] : chunks) {
            static_cast<void>(id);
            send_frame(socket, make_chunk_offer(chunk));
            const auto response = receive_frame(socket);
            if (response.type != ProtocolMessageType::chunk_needed
                || response.payload.size() != 1U
                || response.payload[0] > 1U) {
                throw std::runtime_error("invalid chunk negotiation response");
            }
            if (response.payload[0] == 0U) {
                ++result.chunks_reused;
                continue;
            }
            auto contents = read_chunk_contents(source, chunk);
            std::vector<std::uint8_t> payload;
            payload.reserve(chunk.digest.size() + contents.size());
            payload.insert(payload.end(), chunk.digest.begin(), chunk.digest.end());
            payload.insert(payload.end(), contents.begin(), contents.end());
            send_frame(socket, {
                ProtocolMessageType::chunk_data,
                std::move(payload),
            });
            ++result.chunks_transferred;
            result.bytes_transferred += chunk.size;
        }
        send_frame(socket, {ProtocolMessageType::complete, {}});
        const auto completion = receive_frame(socket);
        if (completion.type != ProtocolMessageType::complete
            || !completion.payload.empty()) {
            throw std::runtime_error("invalid synchronization completion response");
        }
        close_socket(socket);
        cleanup_socket_runtime();
        return result;
    } catch (...) {
        close_socket(socket);
        cleanup_socket_runtime();
        throw;
    }
}

HandshakeResult perform_protocol_handshake(const std::string& host,
                                           std::uint16_t port)
{
    if (host.empty() || port == 0U) {
        throw std::invalid_argument("protocol server host and port are required");
    }
    initialize_socket_runtime();
    NativeSocket socket = invalid_socket;
    try {
        socket = connect_to(host, port);
        configure_timeouts(socket);
        const auto version = exchange_client_hello(socket);
        close_socket(socket);
        cleanup_socket_runtime();
        return {version, host + ":" + std::to_string(port)};
    } catch (...) {
        close_socket(socket);
        cleanup_socket_runtime();
        throw;
    }
}

}  // namespace syncvault
