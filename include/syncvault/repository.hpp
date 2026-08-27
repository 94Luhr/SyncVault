#pragma once

#include <filesystem>

namespace syncvault {

class Repository {
public:
    static Repository initialize(const std::filesystem::path& root);
    static bool is_repository(const std::filesystem::path& root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;

private:
    explicit Repository(std::filesystem::path root);

    std::filesystem::path root_;
};

}  // namespace syncvault
