#include "syncvault/network.hpp"

#include "syncvault/protocol.hpp"
#include "syncvault/repository.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
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
        non_repository_server_is_rejected();
        std::cout << "All network tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
