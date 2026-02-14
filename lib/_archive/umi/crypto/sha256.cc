// SPDX-License-Identifier: MIT
// SHA-256 Hash Function (FIPS 180-4)

#include "sha256.hh"

#include <cstring>

namespace umi::crypto {

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

constexpr uint32_t rotr(uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr uint32_t sig0(uint32_t x) noexcept {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

constexpr uint32_t sig1(uint32_t x) noexcept {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

constexpr uint32_t ep0(uint32_t x) noexcept {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

constexpr uint32_t ep1(uint32_t x) noexcept {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

void transform(Sha256Context& ctx, const uint8_t* block) noexcept {
    uint32_t w[64];

    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) |
               (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) |
               block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = ep1(w[i - 2]) + w[i - 7] + ep0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
    uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + sig1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = sig0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx.state[0] += a; ctx.state[1] += b; ctx.state[2] += c; ctx.state[3] += d;
    ctx.state[4] += e; ctx.state[5] += f; ctx.state[6] += g; ctx.state[7] += h;
}

} // namespace

void sha256_init(Sha256Context& ctx) noexcept {
    ctx.state = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    ctx.buffer_len = 0;
    ctx.total_len = 0;
}

void sha256_update(Sha256Context& ctx, const uint8_t* data, size_t len) noexcept {
    for (size_t i = 0; i < len; ++i) {
        ctx.buffer[ctx.buffer_len++] = data[i];
        if (ctx.buffer_len == SHA256_BLOCK_SIZE) {
            transform(ctx, ctx.buffer.data());
            ctx.buffer_len = 0;
        }
    }
    ctx.total_len += len;
}

void sha256_final(Sha256Context& ctx, uint8_t* hash) noexcept {
    ctx.buffer[ctx.buffer_len++] = 0x80;
    if (ctx.buffer_len > 56) {
        while (ctx.buffer_len < SHA256_BLOCK_SIZE) ctx.buffer[ctx.buffer_len++] = 0;
        transform(ctx, ctx.buffer.data());
        ctx.buffer_len = 0;
    }
    while (ctx.buffer_len < 56) ctx.buffer[ctx.buffer_len++] = 0;

    uint64_t bits = ctx.total_len * 8;
    ctx.buffer[56] = static_cast<uint8_t>(bits >> 56);
    ctx.buffer[57] = static_cast<uint8_t>(bits >> 48);
    ctx.buffer[58] = static_cast<uint8_t>(bits >> 40);
    ctx.buffer[59] = static_cast<uint8_t>(bits >> 32);
    ctx.buffer[60] = static_cast<uint8_t>(bits >> 24);
    ctx.buffer[61] = static_cast<uint8_t>(bits >> 16);
    ctx.buffer[62] = static_cast<uint8_t>(bits >> 8);
    ctx.buffer[63] = static_cast<uint8_t>(bits);
    transform(ctx, ctx.buffer.data());

    for (int i = 0; i < 8; ++i) {
        hash[i * 4]     = static_cast<uint8_t>(ctx.state[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(ctx.state[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(ctx.state[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i]);
    }
}

void sha256(const uint8_t* data, size_t len, uint8_t* hash) noexcept {
    Sha256Context ctx;
    sha256_init(ctx);
    sha256_update(ctx, data, len);
    sha256_final(ctx, hash);
}

void hmac_sha256(const uint8_t* key, size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t* out) noexcept {
    uint8_t k_pad[SHA256_BLOCK_SIZE]{};

    if (key_len > SHA256_BLOCK_SIZE) {
        sha256(key, key_len, k_pad);
        key_len = SHA256_HASH_SIZE;
    } else {
        std::memcpy(k_pad, key, key_len);
    }

    // Inner hash: H((K ^ ipad) || data)
    uint8_t inner_pad[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        inner_pad[i] = k_pad[i] ^ 0x36;
    }

    Sha256Context inner;
    sha256_init(inner);
    sha256_update(inner, inner_pad, SHA256_BLOCK_SIZE);
    sha256_update(inner, data, data_len);
    uint8_t inner_hash[SHA256_HASH_SIZE];
    sha256_final(inner, inner_hash);

    // Outer hash: H((K ^ opad) || inner_hash)
    uint8_t outer_pad[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        outer_pad[i] = k_pad[i] ^ 0x5c;
    }

    Sha256Context outer;
    sha256_init(outer);
    sha256_update(outer, outer_pad, SHA256_BLOCK_SIZE);
    sha256_update(outer, inner_hash, SHA256_HASH_SIZE);
    sha256_final(outer, out);
}

} // namespace umi::crypto
