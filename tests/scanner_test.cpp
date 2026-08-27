#include "syncvault/scanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
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

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("syncvault-scanner-test-" + std::to_string(unique));
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

const syncvault::FileEntry& find_entry(
    const std::vector<syncvault::FileEntry>& entries,
    const std::filesystem::path& path)
{
    for (const auto& entry : entries) {
        if (entry.relative_path == path) {
            return entry;
        }
    }
    throw std::runtime_error("expected scan entry was not found");
}

void scan_collects_files_and_directories()
{
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "source";
    std::filesystem::create_directories(root / "empty");
    std::filesystem::create_directories(root / "nested");

    std::ofstream(root / "file.txt", std::ios::binary) << "hello";
    std::ofstream(root / "nested" / "child.bin", std::ios::binary) << "abc";

    const auto entries = syncvault::scan_directory(root);

    require(entries.size() == 4, "scan should return two files and two directories");
    require(find_entry(entries, "empty").type == syncvault::EntryType::directory,
            "empty directory type is incorrect");
    require(find_entry(entries, "nested").type == syncvault::EntryType::directory,
            "nested directory type is incorrect");

    const auto& file = find_entry(entries, "file.txt");
    require(file.type == syncvault::EntryType::regular_file,
            "regular file type is incorrect");
    require(file.size == 5, "regular file size is incorrect");
    require(file.modified_time_ns > 0,
            "last-write time should use the Unix epoch");

    const auto& child = find_entry(entries, std::filesystem::path("nested") / "child.bin");
    require(child.type == syncvault::EntryType::regular_file,
            "nested file type is incorrect");
    require(child.size == 3, "nested file size is incorrect");

    for (std::size_t index = 1; index < entries.size(); ++index) {
        require(entries[index - 1].relative_path.generic_u8string()
                    < entries[index].relative_path.generic_u8string(),
                "scan results should be sorted by relative path");
    }
}

void scan_rejects_regular_file_root()
{
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "file.txt";
    std::ofstream(file) << "not a directory";

    bool failed = false;
    try {
        static_cast<void>(syncvault::scan_directory(file));
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed, "scan should reject a regular file as its root");
}

}  // namespace

int main()
{
    try {
        scan_collects_files_and_directories();
        scan_rejects_regular_file_root();
        std::cout << "All scanner tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
