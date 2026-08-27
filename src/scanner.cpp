#include "syncvault/scanner.hpp"

#include <algorithm>
#include <chrono>
#include <ratio>
#include <stdexcept>
#include <system_error>

namespace syncvault {
namespace {

EntryType classify(std::filesystem::file_status status) noexcept
{
    if (std::filesystem::is_regular_file(status)) {
        return EntryType::regular_file;
    }
    if (std::filesystem::is_directory(status)) {
        return EntryType::directory;
    }
    if (std::filesystem::is_symlink(status)) {
        return EntryType::symlink;
    }
    return EntryType::other;
}

std::int64_t modified_time_ns(const std::filesystem::directory_entry& entry)
{
    std::error_code error;
    const auto time = entry.last_write_time(error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read last-write time", entry.path(), error);
    }

    #ifdef _MSC_VER
    // MSVC stores filesystem times as 100 ns ticks since the Windows epoch
    // (1601-01-01). Subtract before converting to nanoseconds to avoid overflow.
    using FiletimeDuration = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
    constexpr std::int64_t windows_to_unix_epoch_ticks = 116'444'736'000'000'000;
    const auto ticks = std::chrono::duration_cast<FiletimeDuration>(
                           time.time_since_epoch())
                           .count();
    return (ticks - windows_to_unix_epoch_ticks) * 100;
#else
    // C++17-compatible conversion for standard libraries without clock_cast.
    const auto system_time = std::chrono::system_clock::now()
        + (time - std::filesystem::file_time_type::clock::now());
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               system_time.time_since_epoch())
        .count();
#endif
}

FileEntry read_entry(const std::filesystem::directory_entry& entry,
                     const std::filesystem::path& root)
{
    std::error_code error;
    const auto status = entry.symlink_status(error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read file status", entry.path(), error);
    }

    const auto type = classify(status);
    std::uintmax_t size = 0;
    if (type == EntryType::regular_file) {
        size = entry.file_size(error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "cannot read file size", entry.path(), error);
        }
    }

    const auto relative_path = entry.path().lexically_relative(root);
    if (relative_path.empty()) {
        throw std::runtime_error("cannot determine path relative to scan root");
    }

    return FileEntry{
        relative_path.lexically_normal(),
        type,
        size,
        modified_time_ns(entry),
    };
}

}  // namespace

std::vector<FileEntry> scan_directory(const std::filesystem::path& root)
{
    if (root.empty()) {
        throw std::invalid_argument("source path must not be empty");
    }

    const auto absolute_root = std::filesystem::absolute(root).lexically_normal();
    std::error_code error;
    const auto status = std::filesystem::status(absolute_root, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot access source path", absolute_root, error);
    }
    if (!std::filesystem::is_directory(status)) {
        throw std::invalid_argument("source path must be a directory");
    }

    std::vector<FileEntry> entries;
    std::filesystem::recursive_directory_iterator iterator(absolute_root, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot scan source directory", absolute_root, error);
    }

    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        entries.push_back(read_entry(*iterator, absolute_root));
        iterator.increment(error);
        if (error) {
            throw std::filesystem::filesystem_error(
                "cannot continue directory scan", absolute_root, error);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.relative_path.generic_u8string()
            < right.relative_path.generic_u8string();
    });
    return entries;
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

std::string_view to_string(EntryType type) noexcept
{
    switch (type) {
    case EntryType::regular_file:
        return "file";
    case EntryType::directory:
        return "directory";
    case EntryType::symlink:
        return "symlink";
    case EntryType::other:
        return "other";
    }
    return "other";
}

}  // namespace syncvault
