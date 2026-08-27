#include "syncvault/chunker.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace syncvault {

std::vector<ChunkDescriptor> chunk_file(const std::filesystem::path& path,
                                        std::size_t chunk_size)
{
    if (chunk_size == 0U) {
        throw std::invalid_argument("chunk size must be greater than zero");
    }
    if (chunk_size > static_cast<std::size_t>(
                         std::numeric_limits<std::streamsize>::max())) {
        throw std::invalid_argument("chunk size exceeds stream limits");
    }

    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot access file for chunking", path, error);
    }
    if (!std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("chunk source must be a regular file");
    }

    const auto expected_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read source file size", path, error);
    }
    const auto expected_write_time = std::filesystem::last_write_time(path, error);
    if (error) {
        throw std::filesystem::filesystem_error(
            "cannot read source last-write time", path, error);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open source file for chunking");
    }

    std::vector<std::uint8_t> buffer(chunk_size);
    std::vector<ChunkDescriptor> chunks;
    std::uint64_t offset = 0U;

    while (true) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto bytes_read = input.gcount();
        if (bytes_read <= 0) {
            break;
        }

        const auto size = static_cast<std::size_t>(bytes_read);
        chunks.push_back(ChunkDescriptor{
            offset,
            static_cast<std::uint64_t>(size),
            sha256(std::span<const std::uint8_t>(buffer.data(), size)),
        });
        offset += static_cast<std::uint64_t>(size);
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading source file");
    }
    if (offset != expected_size) {
        throw std::runtime_error("source file changed while it was being chunked");
    }
    const auto final_size = std::filesystem::file_size(path, error);
    if (error || final_size != expected_size) {
        throw std::runtime_error("source file changed while it was being chunked");
    }
    const auto final_write_time = std::filesystem::last_write_time(path, error);
    if (error || final_write_time != expected_write_time) {
        throw std::runtime_error("source file changed while it was being chunked");
    }
    return chunks;
}

}  // namespace syncvault