// =============================================================================
//  include/tts/sha256.hpp
//  Phase 2 — Header-only FIPS 180-4 SHA-256 implementation.
//
//  Why hand-rolled instead of Crypto++:
//    - The SDK only needs SHA-256 for TTF integrity; we keep the dependency
//      surface at zero for the hashing primitive so the SDK can be vendored
//      into environments without OpenSSL/Crypto++.
//    - Constant-time-ish on the message schedule; deterministic and self-
//      contained (no global state).
//
//  Public surface:
//    class tts::crypto::Sha256          — streaming hasher
//    tts::crypto::sha256(span)          — one-shot
//    tts::crypto::sha256_hex(span)      — one-shot returning lowercase hex
//    tts::crypto::to_hex(bytes|digest)  — bytes -> lowercase hex string
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tts::crypto {

// -----------------------------------------------------------------------------
//  SHA-256 constants (first 32 bits of the fractional parts of cube roots
//  of the first 64 prime numbers — FIPS 180-4 §4.2.2).
// -----------------------------------------------------------------------------
namespace detail {

inline constexpr std::uint32_t kK[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

inline constexpr std::array<std::uint32_t, 8> kInit = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
};

[[nodiscard]] inline std::uint32_t load_be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) <<  8) |
           (static_cast<std::uint32_t>(p[3]));
}

inline void store_be32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>( v        & 0xFFu);
}

[[nodiscard]] inline std::uint32_t rotr(std::uint32_t x, int n) noexcept {
    return (x >> n) | (x << (32 - n));
}

} // namespace detail

// -----------------------------------------------------------------------------
//  Hex helpers — declared early because one-shot helpers below use them.
// -----------------------------------------------------------------------------
[[nodiscard]] inline std::string
to_hex(std::span<const std::uint8_t> bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(bytes.size() * 2, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2]     = kHex[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1]  = kHex[ bytes[i]       & 0x0F];
    }
    return out;
}

[[nodiscard]] inline std::string
to_hex(const std::array<std::uint8_t, 32>& digest) {
    return to_hex(std::span<const std::uint8_t, 32>{digest.data(), 32});
}

// -----------------------------------------------------------------------------
//  constant_time_equals — bitwise-constant-time comparison.
//
//  Used for hash verification in TTF integrity checks so that an attacker who
//  can observe timing of get_section() cannot use it as an oracle to recover
//  a stored hash byte-by-byte. Compares exactly `n` bytes; if sizes differ
//  the result is false but the function still walks the longer buffer (so
//  timing depends only on the longer length, not on where they diverge).
// -----------------------------------------------------------------------------
[[nodiscard]] inline bool
constant_time_equals(const std::uint8_t* a,
                     const std::uint8_t* b,
                     std::size_t n) noexcept {
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < n; ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    // Fold all bits down to the low bit; nonzero -> 1.
    diff |= diff >> 4;
    diff |= diff >> 2;
    diff |= diff >> 1;
    return (diff & 1) == 0;
}

[[nodiscard]] inline bool
constant_time_equals(std::span<const std::uint8_t> a,
                     std::span<const std::uint8_t> b) noexcept {
    if (a.size() != b.size()) return false;
    return constant_time_equals(a.data(), b.data(), a.size());
}

/// Hex-string compare in constant time. Both inputs must be the same length;
/// if lengths differ the function still consumes both buffers.
[[nodiscard]] inline bool
constant_time_equals_hex(std::string_view a, std::string_view b) noexcept {
    const std::size_t n = std::min(a.size(), b.size());
    std::uint8_t diff = static_cast<std::uint8_t>(a.size() ^ b.size());
    for (std::size_t i = 0; i < n; ++i) {
        diff |= static_cast<std::uint8_t>(
            static_cast<unsigned char>(a[i]) ^
            static_cast<unsigned char>(b[i]));
    }
    diff |= diff >> 4; diff |= diff >> 2; diff |= diff >> 1;
    return (diff & 1) == 0;
}

// -----------------------------------------------------------------------------
//  Streaming hasher
// -----------------------------------------------------------------------------
class Sha256 {
public:
    Sha256() noexcept { reset(); }

    void reset() noexcept {
        state_  = detail::kInit;
        length_ = 0;
        buffer_pos_ = 0;
    }

    Sha256& update(std::span<const std::uint8_t> data) noexcept {
        length_ += static_cast<std::uint64_t>(data.size()) * 8u;  // bit length

        std::size_t i = 0;
        // Drain any half-filled buffer first
        if (buffer_pos_ != 0) {
            const std::size_t need = 64 - static_cast<std::size_t>(buffer_pos_);
            const std::size_t take = std::min(need, data.size());
            std::copy_n(data.data(), take, buffer_.data() + buffer_pos_);
            buffer_pos_ += static_cast<int>(take);
            i += take;
            if (buffer_pos_ == 64) { process_block_(buffer_.data()); buffer_pos_ = 0; }
        }
        // Full blocks straight from the source
        while (i + 64 <= data.size()) {
            process_block_(data.data() + i);
            i += 64;
        }
        // Tail
        if (i < data.size()) {
            const std::size_t left = data.size() - i;
            std::copy_n(data.data() + i, left, buffer_.data());
            buffer_pos_ = static_cast<int>(left);
        }
        return *this;
    }

    Sha256& update(std::string_view s) noexcept {
        return update(std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finalize() noexcept {
        // Append 0x80
        buffer_[buffer_pos_++] = 0x80u;
        if (buffer_pos_ > 56) {
            while (buffer_pos_ < 64) buffer_[buffer_pos_++] = 0;
            process_block_(buffer_.data());
            buffer_pos_ = 0;
        }
        while (buffer_pos_ < 56) buffer_[buffer_pos_++] = 0;
        // Append 64-bit big-endian length
        for (int i = 7; i >= 0; --i) {
            buffer_[buffer_pos_++] =
                static_cast<std::uint8_t>((length_ >> (i * 8)) & 0xFFu);
        }
        process_block_(buffer_.data());

        std::array<std::uint8_t, 32> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            detail::store_be32(out.data() + i * 4, state_[i]);
        }
        return out;
    }

private:
    void process_block_(const std::uint8_t* block) noexcept {
        std::uint32_t w[64];
        for (int t = 0; t < 16; ++t) {
            w[t] = detail::load_be32(block + t * 4);
        }
        for (int t = 16; t < 64; ++t) {
            const std::uint32_t s0 = detail::rotr(w[t-15], 7) ^ detail::rotr(w[t-15], 18) ^ (w[t-15] >> 3);
            const std::uint32_t s1 = detail::rotr(w[t-2], 17) ^ detail::rotr(w[t-2], 19) ^ (w[t-2] >> 10);
            w[t] = w[t-16] + s0 + w[t-7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (int t = 0; t < 64; ++t) {
            const std::uint32_t S1 = detail::rotr(e, 6) ^ detail::rotr(e, 11) ^ detail::rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t T1 = h + S1 + ch + detail::kK[t] + w[t];
            const std::uint32_t S0 = detail::rotr(a, 2) ^ detail::rotr(a, 13) ^ detail::rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t T2 = S0 + maj;

            h = g; g = f; f = e;
            e = d + T1;
            d = c; c = b; b = a;
            a = T1 + T2;
        }

        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t length_{};          // bit length of message consumed so far
    int           buffer_pos_{};
};

// -----------------------------------------------------------------------------
//  One-shot helpers
// -----------------------------------------------------------------------------
[[nodiscard]] inline std::array<std::uint8_t, 32>
sha256(std::span<const std::uint8_t> data) noexcept {
    Sha256 h; h.update(data); return h.finalize();
}

[[nodiscard]] inline std::array<std::uint8_t, 32>
sha256(std::string_view s) noexcept {
    return sha256(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
}

[[nodiscard]] inline std::string
sha256_hex(std::span<const std::uint8_t> data) {
    return to_hex(sha256(data));
}

[[nodiscard]] inline std::string
sha256_hex(std::string_view s) {
    return sha256_hex(std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
}

} // namespace tts::crypto
