#include "syncvault/repository.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace syncvault {
namespace {

constexpr auto config_filename = "config";

std::string utc_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};

#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

void write_config_atomically(const std::filesystem::path& root)
{
    const auto temporary = root / "config.tmp";
    const auto destination = root / config_filename;

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create repository configuration");
        }

        output << "format_version=1\n";
        output << "created_at=" << utc_timestamp() << '\n';
        output.flush();

        if (!output) {
            throw std::runtime_error("cannot write repository configuration");
        }
    }

    std::filesystem::rename(temporary, destination);
}

}  // namespace

Repository::Repository(std::filesystem::path root)
    : root_(std::move(root))
{
}

Repository Repository::initialize(const std::filesystem::path& root)
{
    if (root.empty()) {
        throw std::invalid_argument("repository path must not be empty");
    }

    const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
    if (is_repository(absolute_root)) {
        throw std::runtime_error("a SyncVault repository already exists at this path");
    }

    std::filesystem::create_directories(absolute_root / "chunks");
    std::filesystem::create_directories(absolute_root / "snapshots");
    std::filesystem::create_directories(absolute_root / "tmp");

    try {
        write_config_atomically(absolute_root);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(absolute_root / "config.tmp", ignored);
        throw;
    }

    return Repository(absolute_root);
}

bool Repository::is_repository(const std::filesystem::path& root)
{
    std::error_code error;
    return std::filesystem::is_regular_file(root / config_filename, error)
        && std::filesystem::is_directory(root / "chunks", error)
        && std::filesystem::is_directory(root / "snapshots", error);
}

const std::filesystem::path& Repository::root() const noexcept
{
    return root_;
}

}  // namespace syncvault
