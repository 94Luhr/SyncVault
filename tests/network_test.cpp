#include "syncvault/network.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/manifest.hpp"
#include "syncvault/protocol.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/restore.hpp"
#include "syncvault/snapshot.hpp"
#include "syncvault/verify.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("syncvault-network-test-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void client_and_server_complete_handshake()
{
    TemporaryDirectory temporary;
    const auto repository = temporary.path() / "repository";
    syncvault::Repository::initialize(repository);
    syncvault::ProtocolHandshakeServer server(repository, 0U);
    require(server.local_port() != 0U,
            "operating system did not assign a listener port");

    syncvault::HandshakeResult server_result;
    std::exception_ptr server_error;
    std::thread server_thread([&] {
        try {
            server_result = server.accept_once();
        } catch (...) {
            server_error = std::current_exception();
        }
    });

    syncvault::HandshakeResult client_result;
    try {
        client_result = syncvault::perform_protocol_handshake(
            "127.0.0.1", server.local_port());
    } catch (...) {
        server_thread.join();
        throw;
    }
    server_thread.join();
    if (server_error) {
        std::rethrow_exception(server_error);
    }

    require(client_result.protocol_version == syncvault::protocol_version,
            "client negotiated the wrong protocol version");
    require(server_result.protocol_version == syncvault::protocol_version,
            "server negotiated the wrong protocol version");
    require(!server_result.peer.empty(), "server did not report its peer");
}

void chunks_transfer_incrementally()
{
    TemporaryDirectory temporary;
    const auto source_repository = temporary.path() / "source-repository";
    const auto destination_repository =
        temporary.path() / "destination-repository";
    const auto source_data = temporary.path() / "source-data";
    syncvault::Repository::initialize(source_repository);
    syncvault::Repository::initialize(destination_repository);
    std::filesystem::create_directories(source_data);
    std::ofstream(source_data / "data.bin", std::ios::binary) << "abcdefghij";
    const auto snapshot =
        syncvault::create_snapshot(source_repository, source_data, 4U);
    syncvault::ProtocolHandshakeServer server(destination_repository, 0U);

    auto run_once = [&](syncvault::NetworkChunkSyncResult& server_result) {
        std::exception_ptr server_error;
        std::thread server_thread([&] {
            try {
                server_result = server.accept_chunk_sync_once();
            } catch (...) {
                server_error = std::current_exception();
            }
        });
        syncvault::NetworkChunkSyncResult client_result;
        try {
            client_result = syncvault::push_repository_chunks(
                source_repository, "127.0.0.1", server.local_port());
        } catch (...) {
            server_thread.join();
            throw;
        }
        server_thread.join();
        if (server_error) {
            std::rethrow_exception(server_error);
        }
        return client_result;
    };

    syncvault::NetworkChunkSyncResult first_server;
    const auto first_client = run_once(first_server);
    require(first_client.chunks_transferred == 3U
                && first_client.chunks_reused == 0U
                && first_client.bytes_transferred == 10U,
            "first network sync should transfer every unique chunk");
    require(first_server.chunks_transferred == 3U
                && first_server.bytes_transferred == 10U,
            "server transfer statistics are incorrect");
    require(first_client.manifests_transferred == 1U
                && first_server.manifests_transferred == 1U,
            "first network sync should transfer its snapshot manifest");
    require(syncvault::verify_repository(destination_repository).healthy(),
            "network synchronized repository should pass verification");
    const auto restored = temporary.path() / "restored";
    static_cast<void>(syncvault::restore_snapshot(
        destination_repository, snapshot.snapshot.id, restored));
    std::ifstream restored_input(restored / "data.bin", std::ios::binary);
    const std::string restored_text{
        std::istreambuf_iterator<char>(restored_input),
        std::istreambuf_iterator<char>()};
    require(restored_text == "abcdefghij",
            "network synchronized snapshot should restore byte-for-byte");

    const auto manifest =
        syncvault::read_snapshot_manifest(source_repository, snapshot.snapshot.id);
    for (const auto& entry : manifest.entries) {
        for (const auto& chunk : entry.chunks) {
            require(syncvault::has_verified_chunk(
                        destination_repository, chunk.digest, chunk.size),
                    "destination is missing a verified network chunk");
        }
    }

    syncvault::NetworkChunkSyncResult second_server;
    const auto second_client = run_once(second_server);
    require(second_client.chunks_transferred == 0U
                && second_client.chunks_reused == 3U
                && second_client.bytes_transferred == 0U,
            "repeated network sync should transfer no chunk bytes");
    require(second_server.chunks_transferred == 0U
                && second_server.chunks_reused == 3U,
            "server should reuse every existing chunk");
    require(second_client.manifests_transferred == 0U
                && second_client.manifests_reused == 1U
                && second_server.manifests_reused == 1U,
            "repeated network sync should reuse its snapshot manifest");
}

void authentication_accepts_matching_and_rejects_wrong_tokens()
{
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "source";
    const auto destination = temporary.path() / "destination";
    syncvault::Repository::initialize(source);
    syncvault::Repository::initialize(destination);

    auto attempt = [&](const std::string& client_token, bool should_succeed) {
        syncvault::ProtocolHandshakeServer server(
            destination, 0U, "correct horse battery staple");
        std::exception_ptr server_error;
        std::thread server_thread([&] {
            try {
                static_cast<void>(server.accept_chunk_sync_once());
            } catch (...) {
                server_error = std::current_exception();
            }
        });
        bool client_succeeded = true;
        try {
            static_cast<void>(syncvault::push_repository_chunks(
                source, "127.0.0.1", server.local_port(), client_token));
        } catch (...) {
            client_succeeded = false;
        }
        server_thread.join();
        require(client_succeeded == should_succeed,
                "authentication client result is incorrect");
        require((server_error == nullptr) == should_succeed,
                "authentication server result is incorrect");
    };

    attempt("correct horse battery staple", true);
    attempt("wrong token", false);
}

void authenticated_server_supports_configurable_binding()
{
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "source";
    const auto destination = temporary.path() / "destination";
    syncvault::Repository::initialize(source);
    syncvault::Repository::initialize(destination);

    bool unauthenticated_rejected = false;
    try {
        syncvault::ProtocolHandshakeServer unsafe(
            destination, 0U, {}, "0.0.0.0");
        static_cast<void>(unsafe);
    } catch (const std::invalid_argument&) {
        unauthenticated_rejected = true;
    }
    require(unauthenticated_rejected,
            "non-loopback listener should require authentication");

    syncvault::ProtocolHandshakeServer server(
        destination, 0U, "lan-test-token", "0.0.0.0");
    require(server.bind_address() == "0.0.0.0",
            "server did not retain its configured bind address");
    std::exception_ptr server_error;
    std::thread server_thread([&] {
        try {
            static_cast<void>(server.accept_chunk_sync_once());
        } catch (...) {
            server_error = std::current_exception();
        }
    });
    static_cast<void>(syncvault::push_repository_chunks(
        source, "127.0.0.1", server.local_port(), "lan-test-token"));
    server_thread.join();
    if (server_error) {
        std::rethrow_exception(server_error);
    }
}

void non_repository_server_is_rejected()
{
    TemporaryDirectory temporary;
    bool rejected = false;
    try {
        syncvault::ProtocolHandshakeServer server(temporary.path(), 0U);
        static_cast<void>(server);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "server should reject a non-repository path");
}

}  // namespace

int main()
{
    try {
        client_and_server_complete_handshake();
        chunks_transfer_incrementally();
        authentication_accepts_matching_and_rejects_wrong_tokens();
        authenticated_server_supports_configurable_binding();
        non_repository_server_is_rejected();
        std::cout << "All network tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
