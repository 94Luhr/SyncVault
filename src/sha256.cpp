#include "syncvault/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <vector>

namespace syncvault {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32U - count));
}

std::uint32_t read_big_endian(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24U)
        | (static_cast<std::uint32_t>(data[1]) << 16U)
        | (static_cast<std::uint32_t>(data[2]) << 8U)
        | static_cast<std::uint32_t>(data[3]);
}

void write_big_endian(std::uint32_t value, std::uint8_t* output)
{
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}

}  // namespace

Sha256Digest sha256(std::span<const std::uint8_t> input)
{
    if (input.size() > std::numeric_limits<std::uint64_t>::max() / 8U
        || input.size() > std::numeric_limits<std::size_t>::max() - 72U) {
        throw std::length_error("SHA-256 input is too large");
    }

    const auto bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    const auto padded_size = ((input.size() + 9U + 63U) / 64U) * 64U;
    std::vector<std::uint8_t> message(padded_size, 0U);
    std::copy(input.begin(), input.end(), message.begin());
    message[input.size()] = 0x80U;

    for (std::size_t index = 0; index < 8U; ++index) {
        message[padded_size - 1U - index] = static_cast<std::uint8_t>(
            bit_length >> (index * 8U));
    }

    std::array<std::uint32_t, 8> state = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    for (std::size_t block = 0; block < message.size(); block += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            words[index] = read_big_endian(message.data() + block + index * 4U);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const auto sigma0 = rotate_right(words[index - 15U], 7U)
                ^ rotate_right(words[index - 15U], 18U)
                ^ (words[index - 15U] >> 3U);
            const auto sigma1 = rotate_right(words[index - 2U], 17U)
                ^ rotate_right(words[index - 2U], 19U)
                ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + sigma0
                + words[index - 7U] + sigma1;
        }

        auto a = state[0];
        auto b = state[1];
        auto c = state[2];
        auto d = state[3];
        auto e = state[4];
        auto f = state[5];
        auto g = state[6];
        auto h = state[7];

        for (std::size_t index = 0; index < words.size(); ++index) {
            const auto sum1 = rotate_right(e, 6U) ^ rotate_right(e, 11U)
                ^ rotate_right(e, 25U);
            const auto choice = (e & f) ^ (~e & g);
            const auto temporary1 = h + sum1 + choice
                + round_constants[index] + words[index];
            const auto sum0 = rotate_right(a, 2U) ^ rotate_right(a, 13U)
                ^ rotate_right(a, 22U);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    Sha256Digest digest{};
    for (std::size_t index = 0; index < state.size(); ++index) {
        write_big_endian(state[index], digest.data() + index * 4U);
    }
    return digest;
}

std::string to_hex(const Sha256Digest& digest)
{
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2U);
    for (std::size_t index = 0; index < digest.size(); ++index) {
        result[index * 2U] = alphabet[digest[index] >> 4U];
        result[index * 2U + 1U] = alphabet[digest[index] & 0x0fU];
    }
    return result;
}

}  // namespace syncvault