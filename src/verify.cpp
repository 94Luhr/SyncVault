#include "syncvault/verify.hpp"

#include "syncvault/manifest.hpp"
#include "syncvault/repository.hpp"
#include "syncvault/sha256.hpp"

#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace syncvault {
namespace {

bool is_lower_hex(std::string_view value)
{
    for (const char character : value) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'f';
        if (!digit && !lower) {
            return false;
        }
    }
    return true;
}

std::string chunk_id_from_path(const std::filesystem::path& path,
                               const std::filesystem::path& chunks_root)
{
    const auto relative = path.lexically_relative(chunks_root);
    auto component = relative.begin();
    if (component == relative.end()) {
        return {};
    }
    const auto prefix = component->string();
    ++component;
    if (component == relative.end()) {
        return {};
    }
    const auto suffix = component->string();
    ++component;
    if (component != relative.end() || prefix.size() != 2U
        || suffix.size() != 62U || !is_lower_hex(prefix)
        || !is_lower_hex(suffix)) {
        return {};
    }
    return prefix + suffix;
}

std::string path_for_message(const std::filesystem::path& path)
{
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void add_issue(VerificationResult& result,
               VerificationIssueKind kind,
               std::string object,
               std::string message)
{
    result.issues.push_back(
        VerificationIssue{kind, std::move(object), std::move(message)});
}

void inspect_chunk(const std::filesystem::directory_entry& entry,
                   const std::filesystem::path& chunks_root,
                   const std::map<std::string, std::uint64_t>& referenced,
                   std::set<std::string>& stored,
                   VerificationResult& result)
{
    const auto id = chunk_id_from_path(entry.path(), chunks_root);
    if (id.empty() || !entry.is_regular_file()) {
        add_issue(result,
                  VerificationIssueKind::invalid_chunk_path,
                  path_for_message(entry.path().lexically_relative(chunks_root)),
                  "chunk is not a regular file at a valid content path");
        return;
    }
    stored.insert(id);
    ++result.chunks_checked;

    std::error_code error;
    const auto size = entry.file_size(error);
    if (error) {
        add_issue(result,
                  VerificationIssueKind::unreadable_chunk,
                  id,
                  "cannot read chunk size");
        return;
    }
    result.bytes_checked += size;

    const auto reference = referenced.find(id);
    if (reference == referenced.end()) {
        ++result.unreferenced_chunks;
    } else if (reference->second != size) {
        add_issue(result,
                  VerificationIssueKind::chunk_size_mismatch,
                  id,
                  "stored size does not match snapshot reference");
    }

    if (size > static_cast<std::uintmax_t>(
                   std::numeric_limits<std::size_t>::max())
        || size > static_cast<std::uintmax_t>(
                      std::numeric_limits<std::streamsize>::max())) {
        add_issue(result,
                  VerificationIssueKind::unreadable_chunk,
                  id,
                  "chunk is too large to verify");
        return;
    }

    std::vector<std::uint8_t> contents(static_cast<std::size_t>(size));
    std::ifstream input(entry.path(), std::ios::binary);
    if (!input) {
        add_issue(result,
                  VerificationIssueKind::unreadable_chunk,
                  id,
                  "cannot open chunk");
        return;
    }
    input.read(reinterpret_cast<char*>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
    if (input.gcount() != static_cast<std::streamsize>(contents.size())
        || input.bad()) {
        add_issue(result,
                  VerificationIssueKind::unreadable_chunk,
                  id,
                  "cannot read complete chunk");
        return;
    }
    if (to_hex(sha256(contents)) != id) {
        add_issue(result,
                  VerificationIssueKind::chunk_digest_mismatch,
                  id,
                  "stored content does not match its SHA-256 path");
    }
}

}  // namespace

VerificationResult verify_repository(
    const std::filesystem::path& repository_root)
{
    if (!Repository::is_repository(repository_root)) {
        throw std::invalid_argument("path is not a SyncVault repository");
    }

    VerificationResult result;
    std::map<std::string, std::uint64_t> referenced;
    for (const auto& entry : std::filesystem::directory_iterator(
             repository_root / "snapshots")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".svsnap") {
            continue;
        }
        const auto id = entry.path().stem().string();
        try {
            const auto manifest = read_snapshot_manifest(repository_root, id);
            ++result.snapshots_checked;
            for (const auto& item : manifest.entries) {
                for (const auto& chunk : item.chunks) {
                    const auto hex = to_hex(chunk.digest);
                    const auto [position, inserted] =
                        referenced.emplace(hex, chunk.size);
                    if (!inserted && position->second != chunk.size) {
                        add_issue(result,
                                  VerificationIssueKind::chunk_size_mismatch,
                                  hex,
                                  "snapshots reference different sizes for one chunk");
                    }
                }
            }
        } catch (const std::exception& error) {
            add_issue(result,
                      VerificationIssueKind::invalid_manifest,
                      id,
                      error.what());
        }
    }

    std::set<std::string> stored;
    const auto chunks_root = repository_root / "chunks";
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(chunks_root)) {
        if (!entry.is_directory()) {
            inspect_chunk(entry, chunks_root, referenced, stored, result);
        }
    }

    for (const auto& [id, size] : referenced) {
        static_cast<void>(size);
        if (!stored.contains(id)) {
            add_issue(result,
                      VerificationIssueKind::missing_chunk,
                      id,
                      "snapshot references a chunk that is not stored");
        }
    }
    return result;
}

std::string_view to_string(VerificationIssueKind kind) noexcept
{
    switch (kind) {
    case VerificationIssueKind::invalid_manifest:
        return "invalid_manifest";
    case VerificationIssueKind::invalid_chunk_path:
        return "invalid_chunk_path";
    case VerificationIssueKind::missing_chunk:
        return "missing_chunk";
    case VerificationIssueKind::chunk_size_mismatch:
        return "chunk_size_mismatch";
    case VerificationIssueKind::chunk_digest_mismatch:
        return "chunk_digest_mismatch";
    case VerificationIssueKind::unreadable_chunk:
        return "unreadable_chunk";
    }
    return "unknown";
}

}  // namespace syncvault
