#include "syncvault/auth.hpp"

#include "syncvault/sha256.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    try {
        const std::string key(20U, static_cast<char>(0x0b));
        constexpr std::array<std::uint8_t, 8> message{
            'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
        const auto digest = syncvault::hmac_sha256(key, message);
        if (syncvault::to_hex(digest)
            != "b0344c61d8db38535ca8afceaf0bf12b"
               "881dc200c9833da726e9376c2e32cff7") {
            throw std::runtime_error("HMAC-SHA256 test vector failed");
        }
        auto changed = digest;
        changed[0] ^= 1U;
        if (!syncvault::constant_time_equal(digest, digest)
            || syncvault::constant_time_equal(digest, changed)) {
            throw std::runtime_error("constant-time comparison failed");
        }
        std::cout << "All authentication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
}
