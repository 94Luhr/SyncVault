#include "syncvault/manifest.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace syncvault {
namespace {

bool valid_snapshot_id(std::string_view id)
{
    if (id.empty() || id.size() > 80U) {
        return false;
    }
    for (const char character : id) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower_hex = character >= 'a' && character <= 'f';
        if (!digit && !lower_hex && character != '-') {
            return false;
        }
    }
    return true;
}

int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    throw std::runtime_error("invalid hexadecimal value in snapshot");
}

std::string hex_decode(std::string_view input)
{
    if (input.size() % 2U != 0U) {
        throw std::runtime_error("invalid hexadecimal path in snapshot");
    }
    std::string result(input.size() / 2U, '\0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto value = (hex_value(input[index * 2U]) << 4)
            | hex_value(input[index * 2U + 1U]);
        if (value == 0) {
            throw std::runtime_error("snapshot path contains a null byte");
        }
        result[index] = static_cast<char>(value);
    }
    return result;
}

std::filesystem::path path_from_utf8(std::string_view input)
{
    std::u8string utf8;
    utf8.reserve(input.size());
    for (const unsigned char value : input) {
        utf8.push_back(static_cast<char8_t>(value));
    }
    return std::filesystem::path(utf8);
}

Sha256Digest digest_from_hex(std::string_view input)
{
    if (input.size() != 64U) {
        throw std::runtime_error("invalid chunk digest in snapshot");
    }
    Sha256Digest digest{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        digest[index] = static_cast<std::uint8_t>(
            (hex_value(input[index * 2U]) << 4)
            | hex_value(input[index * 2U + 1U]));
    }
    return digest;
}

Sha256Digest hash_text(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    return sha256(std::span<const std::uint8_t>(data, text.size()));
}

template <typename Integer>
Integer parse_integer(std::string_view value, std::string_view field)
{
    Integer result{};
    const auto conversion = std::from_chars(
        value.data(), value.data() + value.size(), result);
    if (conversion.ec != std::errc{}
        || conversion.ptr != value.data() + value.size()) {
        throw std::runtime_error(
            "invalid snapshot field: " + std::string(field));
    }
    return result;
}

std::string read_field(std::istream& input, std::string_view expected_key)
{
    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("incomplete snapshot header");
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos
        || std::string_view(line).substr(0U, separator) != expected_key) {
        throw std::runtime_error("invalid snapshot header");
    }
    return line.substr(separator + 1U);
}

std::vector<std::string_view> split_tabs(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0U;
    while (true) {
        const auto separator = line.find('\t', start);
        if (separator == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, separator - start));
        start = separator + 1U;
    }
}

std::filesystem::path parse_relative_path(std::string_view encoded)
{
    const auto path = path_from_utf8(hex_decode(encoded));
    if (path.empty() || path.is_absolute() || path.has_root_path()
        || path.lexically_normal() != path) {
        throw std::runtime_error("unsafe relative path in snapshot");
    }
    for (const auto& component : path) {
        if (component == "." || component == "..") {
            throw std::runtime_error("unsafe relative path in snapshot");
        }
    }
    return path;
}

std::vector<ManifestChunk> parse_chunks(std::string_view field,
                                        std::uint64_t expected_size)
{
    std::vector<ManifestChunk> chunks;
    std::uint64_t total_size = 0U;
    std::size_t start = 0U;
    while (start < field.size()) {
        const auto end = field.find(',', start);
        const auto token = field.substr(
            start, end == std::string_view::npos ? field.size() - start
                                                 : end - start);
        const auto separator = token.find(':');
        if (separator == std::string_view::npos
            || token.find(':', separator + 1U) != std::string_view::npos) {
            throw std::runtime_error("invalid chunk reference in snapshot");
        }
        const auto size = parse_integer<std::uint64_t>(
            token.substr(separator + 1U), "chunk size");
        if (size == 0U
            || total_size > std::numeric_limits<std::uint64_t>::max() - size) {
            throw std::runtime_error("invalid chunk size in snapshot");
        }
        chunks.push_back({digest_from_hex(token.substr(0U, separator)), size});
        total_size += size;
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    if (total_size != expected_size || (expected_size == 0U && !chunks.empty())) {
        throw std::runtime_error("file size does not match snapshot chunks");
    }
    return chunks;
}

std::string read_all(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open snapshot manifest");
    }
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

}  // namespace

bool store_verified_snapshot_manifest(
    const std::filesystem::path& repository_root,
    const std::string& snapshot_id,
    std::span<const std::uint8_t> contents)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (!valid_snapshot_id(snapshot_id)) {
        throw std::invalid_argument("invalid snapshot ID");
    }
    const auto destination =
        repository_root / "snapshots" / (snapshot_id + ".svsnap");
    if (std::filesystem::exists(destination)) {
        const auto existing = read_all(destination);
        if (existing.size() != contents.size()
            || !std::equal(existing.begin(), existing.end(), contents.begin())) {
            throw std::runtime_error("conflicting snapshot manifest");
        }
        static_cast<void>(read_snapshot_manifest(repository_root, snapshot_id));
        return false;
    }
    const auto temporary =
        repository_root / "tmp" / ("network-" + snapshot_id + ".tmp");
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot create temporary manifest");
            }
            output.write(reinterpret_cast<const char*>(contents.data()),
                         static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error("cannot write temporary manifest");
            }
        }
        std::filesystem::rename(temporary, destination);
        const auto manifest =
            read_snapshot_manifest(repository_root, snapshot_id);
        for (const auto& entry : manifest.entries) {
            for (const auto& chunk : entry.chunks) {
                if (!has_verified_chunk(
                        repository_root, chunk.digest, chunk.size)) {
                    throw std::runtime_error(
                        "snapshot references a missing chunk");
                }
            }
        }
        return true;
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(destination, ignored);
        throw;
    }
}

SnapshotManifest read_snapshot_manifest(
    const std::filesystem::path& repository_root,
    const std::string& snapshot_id)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (!valid_snapshot_id(snapshot_id)) {
        throw std::invalid_argument("invalid snapshot ID");
    }

    const auto manifest_path = repository_root / "snapshots"
        / (snapshot_id + ".svsnap");
    const auto contents = read_all(manifest_path);
    const auto first_newline = contents.find('\n');
    const auto second_newline = first_newline == std::string::npos
        ? std::string::npos
        : contents.find('\n', first_newline + 1U);
    if (first_newline == std::string::npos
        || second_newline == std::string::npos
        || std::string_view(contents).substr(0U, first_newline) != "format=1") {
        throw std::runtime_error("invalid snapshot header");
    }

    const auto id_line = std::string_view(contents).substr(
        first_newline + 1U, second_newline - first_newline - 1U);
    if (!id_line.starts_with("id=") || id_line.substr(3U) != snapshot_id) {
        throw std::runtime_error("snapshot ID does not match its manifest");
    }
    const std::string_view body(contents.data() + second_newline + 1U,
                                contents.size() - second_newline - 1U);
    std::istringstream input{std::string(body)};

    SnapshotManifest manifest;
    manifest.summary.id = snapshot_id;
    manifest.summary.created_at_ns = parse_integer<std::int64_t>(
        read_field(input, "created_at_ns"), "created_at_ns");
    manifest.source_root = path_from_utf8(hex_decode(read_field(input, "source")));
    manifest.summary.file_count = parse_integer<std::uint64_t>(
        read_field(input, "files"), "files");
    manifest.summary.directory_count = parse_integer<std::uint64_t>(
        read_field(input, "directories"), "directories");
    manifest.summary.total_bytes = parse_integer<std::uint64_t>(
        read_field(input, "total_bytes"), "total_bytes");

    std::string line;
    if (!std::getline(input, line) || !line.empty()) {
        throw std::runtime_error("invalid snapshot header separator");
    }

    std::set<std::filesystem::path> paths;
    std::uint64_t files = 0U;
    std::uint64_t directories = 0U;
    std::uint64_t total_bytes = 0U;
    while (std::getline(input, line)) {
        if (line.empty()) {
            throw std::runtime_error("empty record in snapshot manifest");
        }
        const auto fields = split_tabs(line);
        ManifestEntry entry;
        if (fields.size() == 3U && fields[0] == "D") {
            entry.type = ManifestEntryType::directory;
            entry.modified_time_ns = parse_integer<std::int64_t>(
                fields[1], "directory modified time");
            entry.relative_path = parse_relative_path(fields[2]);
            ++directories;
        } else if (fields.size() == 5U && fields[0] == "F") {
            entry.type = ManifestEntryType::regular_file;
            entry.size = parse_integer<std::uint64_t>(fields[1], "file size");
            entry.modified_time_ns = parse_integer<std::int64_t>(
                fields[2], "file modified time");
            entry.relative_path = parse_relative_path(fields[3]);
            entry.chunks = parse_chunks(fields[4], entry.size);
            if (total_bytes
                > std::numeric_limits<std::uint64_t>::max() - entry.size) {
                throw std::runtime_error("snapshot byte count overflow");
            }
            total_bytes += entry.size;
            ++files;
        } else {
            throw std::runtime_error("invalid record in snapshot manifest");
        }
        if (!paths.insert(entry.relative_path).second) {
            throw std::runtime_error("duplicate path in snapshot manifest");
        }
        manifest.entries.push_back(std::move(entry));
    }

    if (files != manifest.summary.file_count
        || directories != manifest.summary.directory_count
        || total_bytes != manifest.summary.total_bytes) {
        throw std::runtime_error("snapshot summary does not match its records");
    }
    const auto expected_id = std::to_string(manifest.summary.created_at_ns)
        + "-" + to_hex(hash_text(body)).substr(0U, 12U);
    if (expected_id != snapshot_id) {
        throw std::runtime_error("snapshot manifest integrity check failed");
    }
    return manifest;
}

}  // namespace syncvault
