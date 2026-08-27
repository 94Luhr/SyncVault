#include "syncvault/chunker.hpp"
#include "syncvault/sha256.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

syncvault::Sha256Digest digest_of(std::string_view text)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
    return syncvault::sha256({data, text.size()});
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("syncvault-chunker-test-" + std::to_string(unique));
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

void sha256_matches_standard_vectors()
{
    require(syncvault::to_hex(digest_of(""))
                == "e3b0c44298fc1c149afbf4c8996fb924"
                   "27ae41e4649b934ca495991b7852b855",
            "SHA-256 empty input vector failed");
    require(syncvault::to_hex(digest_of("abc"))
                == "ba7816bf8f01cfea414140de5dae2223"
                   "b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc input vector failed");

    const std::string million_a(1'000'000U, 'a');
    require(syncvault::to_hex(digest_of(million_a))
                == "cdc76e5c9914fb9281a1c7e284d73e67"
                   "f1809a48a497200e046d39ccc7112cd0",
            "SHA-256 million-a input vector failed");
}

void chunk_file_splits_and_hashes_content()
{
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "data.bin";
    std::ofstream(file, std::ios::binary) << "abcdefghij";

    const auto chunks = syncvault::chunk_file(file, 4U);

    require(chunks.size() == 3U, "ten bytes should produce three chunks");
    require(chunks[0].offset == 0U && chunks[0].size == 4U,
            "first chunk range is incorrect");
    require(chunks[1].offset == 4U && chunks[1].size == 4U,
            "second chunk range is incorrect");
    require(chunks[2].offset == 8U && chunks[2].size == 2U,
            "final chunk range is incorrect");
    require(syncvault::to_hex(chunks[0].digest)
                == "88d4266fd4e6338d13b845fcf289579d"
                   "209c897823b9217da3e161936f031589",
            "first chunk SHA-256 digest is incorrect");
}

void empty_file_produces_no_chunks()
{
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "empty.bin";
    std::ofstream(file, std::ios::binary);

    require(syncvault::chunk_file(file, 4U).empty(),
            "empty files should not contain data chunks");
}

void zero_chunk_size_is_rejected()
{
    TemporaryDirectory temporary;
    const auto file = temporary.path() / "data.bin";
    std::ofstream(file, std::ios::binary) << "data";

    bool failed = false;
    try {
        static_cast<void>(syncvault::chunk_file(file, 0U));
    } catch (const std::invalid_argument&) {
        failed = true;
    }
    require(failed, "zero chunk size should be rejected");
}

}  // namespace

int main()
{
    try {
        sha256_matches_standard_vectors();
        chunk_file_splits_and_hashes_content();
        empty_file_produces_no_chunks();
        zero_chunk_size_is_rejected();
        std::cout << "All chunker tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}