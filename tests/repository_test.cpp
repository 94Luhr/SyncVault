#include "syncvault/repository.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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
            / ("syncvault-test-" + std::to_string(unique));
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

void initialize_creates_repository_layout()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "repository";

    const auto repository = syncvault::Repository::initialize(root);

    require(syncvault::Repository::is_repository(root),
            "initialized path should be recognized as a repository");
    require(std::filesystem::is_directory(root / "chunks"),
            "chunks directory is missing");
    require(std::filesystem::is_directory(root / "snapshots"),
            "snapshots directory is missing");
    require(std::filesystem::is_directory(root / "tmp"),
            "temporary directory is missing");
    require(std::filesystem::is_regular_file(root / "config"),
            "configuration file is missing");
    require(repository.root() == std::filesystem::absolute(root).lexically_normal(),
            "repository root should be absolute and normalized");
}

void duplicate_initialization_fails()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "repository";
    syncvault::Repository::initialize(root);

    bool failed = false;
    try {
        syncvault::Repository::initialize(root);
    } catch (const std::runtime_error&) {
        failed = true;
    }

    require(failed, "initializing an existing repository should fail");
}

}  // namespace

int main()
{
    try {
        initialize_creates_repository_layout();
        duplicate_initialization_fails();
        std::cout << "All SyncVault tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
