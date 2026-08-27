#include "syncvault/snapshot.hpp"

#include "syncvault/chunk_store.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/scanner.hpp"
#include "syncvault/sha256.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace syncvault {
namespace {

constexpr std::string_view snapshot_extension = ".svsnap";

std::string hex_encode(std::string_view input)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result(input.size() * 2U, '0');
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto value = static_cast<unsigned char>(input[index]);
        result[index * 2U] = alphabet[value >> 4U];
        result[index * 2U + 1U] = alphabet[value & 0x0fU];
    }
    return result;
}

Sha256Digest hash_text(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    return sha256(std::span<const std::uint8_t>(data, text.size()));
}

bool path_is_within(const std::filesystem::path& child,
                    const std::filesystem::path& parent)
{
    const auto relative = child.lexically_relative(parent);
    if (relative.empty()) {
        return child == parent;
    }
    return *relative.begin() != "..";
}

bool entries_equal(const std::vector<FileEntry>& left,
                   const std::vector<FileEntry>& right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].relative_path != right[index].relative_path
            || left[index].type != right[index].type) {
            return false;
        }
        if (left[index].type == EntryType::regular_file
            && (left[index].size != right[index].size
                || left[index].modified_time_ns
                    != right[index].modified_time_ns)) {
            return false;
        }
    }
    return true;
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

SnapshotSummary read_summary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open snapshot manifest");
    }
    if (read_field(input, "format") != "1") {
        throw std::runtime_error("unsupported snapshot format");
    }

    SnapshotSummary summary;
    summary.id = read_field(input, "id");
    summary.created_at_ns = parse_integer<std::int64_t>(
        read_field(input, "created_at_ns"), "created_at_ns");
    static_cast<void>(read_field(input, "source"));
    summary.file_count = parse_integer<std::uint64_t>(
        read_field(input, "files"), "files");
    summary.directory_count = parse_integer<std::uint64_t>(
        read_field(input, "directories"), "directories");
    summary.total_bytes = parse_integer<std::uint64_t>(
        read_field(input, "total_bytes"), "total_bytes");

    if (path.stem().string() != summary.id) {
        throw std::runtime_error("snapshot ID does not match its filename");
    }
    return summary;
}

}  // namespace

SnapshotResult create_snapshot(const std::filesystem::path& repository_root,
                               const std::filesystem::path& source_root,
                               std::size_t chunk_size)
{
    const auto repository =
        std::filesystem::absolute(repository_root).lexically_normal();
    const auto source =
        std::filesystem::absolute(source_root).lexically_normal();
    if (!Repository::is_repository(repository)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }
    if (path_is_within(repository, source)) {
        throw std::invalid_argument(
            "repository must not be located inside the snapshot source");
    }

    const auto entries = scan_directory(source);
    SnapshotResult result;
    std::ostringstream records;
    for (const auto& entry : entries) {
        const auto encoded_path = hex_encode(path_to_utf8(entry.relative_path));
        if (entry.type == EntryType::directory) {
            ++result.snapshot.directory_count;
            records << "D\t" << entry.modified_time_ns << '\t'
                    << encoded_path << '\n';
            continue;
        }
        if (entry.type != EntryType::regular_file) {
            throw std::runtime_error(
                "snapshot source contains an unsupported file type: "
                + path_to_utf8(entry.relative_path));
        }

        const auto stored = store_file_chunks(
            repository, source / entry.relative_path, chunk_size);
        ++result.snapshot.file_count;
        result.snapshot.total_bytes += entry.size;
        result.chunks_written += stored.chunks_written;
        result.chunks_reused += stored.chunks_reused;
        result.bytes_written += stored.bytes_written;

        records << "F\t" << entry.size << '\t' << entry.modified_time_ns
                << '\t' << encoded_path << '\t';
        for (std::size_t index = 0; index < stored.chunks.size(); ++index) {
            if (index != 0U) {
                records << ',';
            }
            records << to_hex(stored.chunks[index].digest) << ':'
                    << stored.chunks[index].size;
        }
        records << '\n';
    }

    if (!entries_equal(entries, scan_directory(source))) {
        throw std::runtime_error(
            "snapshot source changed while it was being processed");
    }

    result.snapshot.created_at_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    std::ostringstream body;
    body << "created_at_ns=" << result.snapshot.created_at_ns << '\n'
         << "source=" << hex_encode(path_to_utf8(source)) << '\n'
         << "files=" << result.snapshot.file_count << '\n'
         << "directories=" << result.snapshot.directory_count << '\n'
         << "total_bytes=" << result.snapshot.total_bytes << "\n\n"
         << records.str();
    const auto body_text = body.str();
    result.snapshot.id = std::to_string(result.snapshot.created_at_ns) + "-"
        + to_hex(hash_text(body_text)).substr(0U, 12U);

    const auto manifest = "format=1\nid=" + result.snapshot.id + "\n"
        + body_text;
    const auto temporary = repository / "tmp"
        / ("snapshot-" + result.snapshot.id + ".tmp");
    const auto destination = repository / "snapshots"
        / (result.snapshot.id + std::string(snapshot_extension));

    try {
        {
            std::ofstream output(temporary,
                                 std::ios::binary | std::ios::trunc);
            if (!output) {
                throw std::runtime_error("cannot create temporary snapshot");
            }
            output.write(manifest.data(),
                         static_cast<std::streamsize>(manifest.size()));
            output.flush();
            if (!output) {
                throw std::runtime_error("cannot write temporary snapshot");
            }
        }
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
    return result;
}

std::vector<SnapshotSummary> list_snapshots(
    const std::filesystem::path& repository_root)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }

    std::vector<SnapshotSummary> snapshots;
    for (const auto& entry : std::filesystem::directory_iterator(
             repository_root / "snapshots")) {
        if (entry.is_regular_file()
            && entry.path().extension() == snapshot_extension) {
            snapshots.push_back(read_summary(entry.path()));
        }
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const auto& left, const auto& right) {
                  return left.created_at_ns > right.created_at_ns;
              });
    return snapshots;
}

}  // namespace syncvault
