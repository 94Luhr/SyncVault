#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace syncvault {

enum class EntryType {
    regular_file,
    directory,
    symlink,
    other,
};

struct FileEntry {
    std::filesystem::path relative_path;
    EntryType type;
    std::uintmax_t size;
    std::int64_t modified_time_ns;
};

[[nodiscard]] std::vector<FileEntry> scan_directory(
    const std::filesystem::path& root);

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

[[nodiscard]] std::string_view to_string(EntryType type) noexcept;

}  // namespace syncvault
