// SPDX-License-Identifier: MIT
// SHA-256 Hash Function (FIPS 180-4)
// Minimal implementation for HMAC-SHA256 authentication

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace umi::crypto {

/// SHA-256 hash output size in bytes
inline constexpr size_t SHA256_HASH_SIZE = 32;

/// SHA-256 block size in bytes
inline constexpr size_t SHA256_BLOCK_SIZE = 64;

/// SHA-256 context for incremental hashing
struct Sha256Context {
    std::array<uint32_t, 8> state;
    std::array<uint8_t, SHA256_BLOCK_SIZE> buffer;
    size_t buffer_len;
    uint64_t total_len;
};

/// Initialize SHA-256 context
void sha256_init(Sha256Context& ctx) noexcept;

/// Update SHA-256 context with data
void sha256_update(Sha256Context& ctx, const uint8_t* data, size_t len) noexcept;

/// Finalize SHA-256 and get hash
void sha256_final(Sha256Context& ctx, uint8_t* hash) noexcept;

/// One-shot SHA-256 hash
void sha256(const uint8_t* data, size_t len, uint8_t* hash) noexcept;

/// Convenience overload with spans
inline void sha256(std::span<const uint8_t> data, std::span<uint8_t, SHA256_HASH_SIZE> hash) noexcept {
    sha256(data.data(), data.size(), hash.data());
}

/// HMAC-SHA256
void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* out) noexcept;

} // namespace umi::crypto
