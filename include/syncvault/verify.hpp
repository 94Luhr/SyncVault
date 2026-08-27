#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace syncvault {

enum class VerificationIssueKind {
    invalid_manifest,
    invalid_chunk_path,
    missing_chunk,
    chunk_size_mismatch,
    chunk_digest_mismatch,
    unreadable_chunk,
};

struct VerificationIssue {
    VerificationIssueKind kind;
    std::string object;
    std::string message;
};

struct VerificationResult {
    std::uint64_t snapshots_checked = 0U;
    std::uint64_t chunks_checked = 0U;
    std::uint64_t bytes_checked = 0U;
    std::uint64_t unreferenced_chunks = 0U;
    std::vector<VerificationIssue> issues;

    [[nodiscard]] bool healthy() const noexcept { return issues.empty(); }
};

[[nodiscard]] VerificationResult verify_repository(
    const std::filesystem::path& repository_root);

[[nodiscard]] std::string_view to_string(
    VerificationIssueKind kind) noexcept;

}  // namespace syncvault
