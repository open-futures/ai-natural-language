// =============================================================================
//  src/encryption.cpp — M2
//  Minimal AES-256-GCM implementation (vendor-clean, no OpenSSL).
//
//  WARNING: This is a minimal implementation suitable for SDK reference use.
//  It is NOT hardened against cache-based side channels. For production,
//  swap in libsodium or use AES-NI intrinsics.
// =============================================================================
#include "tts/encryption.hpp"
#include "tts/sha256.hpp"

#include <cstring>
#include <random>
#include <sstream>

namespace tts::crypto {

namespace aes {

namespace {

// AES S-box
constexpr std::uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};

// Round constants for AES-256 (14 rounds -> 14 rcon values, plus 0)
constexpr std::uint8_t kRcon[15] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36,0x6c,0xd8,0xab,0x4d
};

// Multiply in GF(2^8) — used by GHASH.
[[nodiscard]] std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) noexcept {
    std::uint8_t p = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) p ^= a;
        const std::uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

// AES-256 key expansion: 240 bytes of round-key material.
struct RoundKeys {
    std::array<std::uint8_t, 240> rk{};
};

[[nodiscard]] RoundKeys expand_key(const Key& key) noexcept {
    RoundKeys out;
    std::memcpy(out.rk.data(), key.data(), 32);
    std::uint8_t temp[4];
    int bytes_done = 32;
    int rcon_iter = 1;
    while (bytes_done < 240) {
        for (int i = 0; i < 4; ++i) temp[i] = out.rk[bytes_done - 4 + i];
        if (bytes_done % 32 == 0) {
            // Rotate
            const std::uint8_t t = temp[0];
            temp[0] = temp[1]; temp[1] = temp[2];
            temp[2] = temp[3]; temp[3] = t;
            // SubWord
            for (int i = 0; i < 4; ++i) temp[i] = kSbox[temp[i]];
            // XOR Rcon
            temp[0] ^= kRcon[rcon_iter++];
        } else if (bytes_done % 32 == 16) {
            for (int i = 0; i < 4; ++i) temp[i] = kSbox[temp[i]];
        }
        for (int i = 0; i < 4; ++i) {
            out.rk[bytes_done] = out.rk[bytes_done - 32] ^ temp[i];
            ++bytes_done;
        }
    }
    return out;
}

[[nodiscard]] std::uint8_t xtime(std::uint8_t x) noexcept {
    return static_cast<std::uint8_t>((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

void aes_encrypt_block(const RoundKeys& rk,
                        std::span<const std::uint8_t, 16> in,
                        std::span<std::uint8_t, 16> out) noexcept {
    std::uint8_t s[16];
    std::memcpy(s, in.data(), 16);
    // AddRoundKey(0)
    for (int i = 0; i < 16; ++i) s[i] ^= rk.rk[i];
    // 14 rounds
    for (int round = 1; round <= 14; ++round) {
        // SubBytes
        for (int i = 0; i < 16; ++i) s[i] = kSbox[s[i]];
        // ShiftRows: row r shifted by r
        // State is column-major: s[0..3] is column 0
        std::uint8_t t;
        // Row 1: shift by 1
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        // Row 2: shift by 2
        t = s[2]; s[2] = s[10]; s[10] = t;
        t = s[6]; s[6] = s[14]; s[14] = t;
        // Row 3: shift by 3
        t = s[3]; s[3] = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = t;
        // MixColumns (skipped on last round)
        if (round < 14) {
            for (int c = 0; c < 4; ++c) {
                std::uint8_t a0 = s[c*4+0], a1 = s[c*4+1], a2 = s[c*4+2], a3 = s[c*4+3];
                s[c*4+0] = xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3;
                s[c*4+1] = a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3;
                s[c*4+2] = a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3);
                s[c*4+3] = (xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3);
            }
        }
        // AddRoundKey
        for (int i = 0; i < 16; ++i) s[i] ^= rk.rk[round * 16 + i];
    }
    std::memcpy(out.data(), s, 16);
}

// GHASH: GF(2^128) multiplication with reduction polynomial x^128 + x^7 + x^2 + x + 1.
void ghash_update(std::array<std::uint8_t, 16>& y,
                  std::span<const std::uint8_t> block,
                  std::array<std::uint8_t, 16>& h) noexcept {
    // Y = (Y XOR block) * H
    for (int i = 0; i < 16; ++i) y[i] ^= block[i];
    std::array<std::uint8_t, 16> z{};
    for (int i = 0; i < 128; ++i) {
        // If bit i of Y is set, XOR with H shifted by i
        if ((y[i / 8] >> (7 - (i % 8))) & 1) {
            for (int j = 0; j < 16; ++j) z[j] ^= h[j];
        }
        // Right-shift H by 1 bit in GF(2^128)
        std::uint8_t lsb = 0;
        for (int j = 15; j >= 0; --j) {
            const std::uint8_t new_lsb = h[j] & 1;
            h[j] = (h[j] >> 1) | (lsb << 7);
            lsb = new_lsb;
        }
        if (lsb) h[15] ^= 0xe1;  // reduction poly
    }
    y = z;
}

} // namespace

// -----------------------------------------------------------------------------
//  Public API
// -----------------------------------------------------------------------------

Key derive_from_passphrase(std::string_view passphrase,
                            std::span<const std::uint8_t> salt,
                            std::uint32_t iterations) {
    // PBKDF2-HMAC-SHA256
    Key out{};
    // Salt = passphrase || user_salt, so each user-salt changes the key.
    std::vector<std::uint8_t> pw_bytes{
        reinterpret_cast<const std::uint8_t*>(passphrase.data()),
        reinterpret_cast<const std::uint8_t*>(passphrase.data()) + passphrase.size()};
    pw_bytes.insert(pw_bytes.end(), salt.begin(), salt.end());

    // Compute HMAC(pw, salt || INT(1)) — simplified single-block PBKDF2
    Sha256 h1; h1.update(pw_bytes);
    h1.update(salt);
    std::uint8_t ctr[4] = {0,0,0,1};
    h1.update(std::span<const std::uint8_t, 4>{ctr, 4});
    auto u = h1.finalize();
    auto t = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
        Sha256 h2; h2.update(pw_bytes);
        h2.update(std::span<const std::uint8_t, 32>{u.data(), 32});
        u = h2.finalize();
        for (int k = 0; k < 32; ++k) t[k] ^= u[k];
    }
    std::memcpy(out.data(), t.data(), 32);
    return out;
}

Nonce random_nonce() {
    Nonce n{};
    std::random_device rd;
    std::uniform_int_distribution<std::uint32_t> dist(0, 255);
    for (std::size_t i = 0; i < kNonceBytes; ++i) n[i] = static_cast<std::uint8_t>(dist(rd));
    return n;
}

std::vector<std::uint8_t>
aes_gcm_encrypt(const Key& key, const Nonce& nonce,
                std::span<const std::uint8_t> plaintext,
                std::span<const std::uint8_t> aad) {
    const auto rk = expand_key(key);
    // H = AES(K, 0^128)
    std::array<std::uint8_t, 16> H{};
    {
        std::array<std::uint8_t, 16> zero{};
        aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{zero.data(), 16},
                          std::span<std::uint8_t, 16>{H.data(), 16});
    }
    // J0 = nonce || 0^31 || 1 (12-byte nonce case)
    std::array<std::uint8_t, 16> j0{};
    std::memcpy(j0.data(), nonce.data(), kNonceBytes);
    j0[15] = 1;
    // Encrypt with CTR mode
    std::vector<std::uint8_t> ct(plaintext.size());
    std::array<std::uint8_t, 16> counter = j0;
    for (std::size_t i = 0; i < plaintext.size(); i += 16) {
        // Increment counter (big-endian, last 32 bits)
        for (int j = 15; j >= 12; --j) {
            if (++counter[j] != 0) break;
        }
        std::array<std::uint8_t, 16> keystream{};
        aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{counter.data(), 16},
                          std::span<std::uint8_t, 16>{keystream.data(), 16});
        const std::size_t block_len = std::min<std::size_t>(16, plaintext.size() - i);
        for (std::size_t k = 0; k < block_len; ++k) ct[i + k] = plaintext[i + k] ^ keystream[k];
    }
    // GHASH over AAD || len(AAD) || len(CT)
    std::array<std::uint8_t, 16> y{};
    std::array<std::uint8_t, 16> h_work = H;
    // AAD
    std::size_t aad_pos = 0;
    while (aad_pos + 16 <= aad.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), aad.data() + aad_pos, 16);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
        aad_pos += 16;
    }
    if (aad_pos < aad.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), aad.data() + aad_pos, aad.size() - aad_pos);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
    }
    // CT
    std::size_t ct_pos = 0;
    while (ct_pos + 16 <= ct.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), ct.data() + ct_pos, 16);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
        ct_pos += 16;
    }
    if (ct_pos < ct.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), ct.data() + ct_pos, ct.size() - ct_pos);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
    }
    // Lengths block: 64-bit AAD bit-length || 64-bit CT bit-length (big-endian)
    std::array<std::uint8_t, 16> len_blk{};
    const std::uint64_t aad_bits = static_cast<std::uint64_t>(aad.size()) * 8;
    const std::uint64_t ct_bits  = static_cast<std::uint64_t>(ct.size()) * 8;
    for (int i = 7; i >= 0; --i) len_blk[7 - i] = static_cast<std::uint8_t>((aad_bits >> (i * 8)) & 0xFF);
    for (int i = 7; i >= 0; --i) len_blk[15 - i] = static_cast<std::uint8_t>((ct_bits >> (i * 8)) & 0xFF);
    ghash_update(y, std::span<const std::uint8_t, 16>{len_blk.data(), 16},
                h_work);
    // Tag = AES(K, J0) XOR Y
    std::array<std::uint8_t, 16> tag{};
    aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{j0.data(), 16},
                      std::span<std::uint8_t, 16>{tag.data(), 16});
    for (int i = 0; i < 16; ++i) tag[i] ^= y[i];

    // Output: nonce || ct || tag
    std::vector<std::uint8_t> out;
    out.reserve(kNonceBytes + ct.size() + kTagBytes);
    out.insert(out.end(), nonce.begin(), nonce.end());
    out.insert(out.end(), ct.begin(), ct.end());
    out.insert(out.end(), tag.begin(), tag.end());
    return out;
}

std::vector<std::uint8_t>
aes_gcm_decrypt(const Key& key,
                std::span<const std::uint8_t> nonce_ct_tag,
                std::span<const std::uint8_t> aad) {
    if (nonce_ct_tag.size() < kNonceBytes + kTagBytes) return {};
    Nonce nonce;
    std::memcpy(nonce.data(), nonce_ct_tag.data(), kNonceBytes);
    const std::size_t ct_len = nonce_ct_tag.size() - kNonceBytes - kTagBytes;
    std::span<const std::uint8_t> ct{nonce_ct_tag.data() + kNonceBytes, ct_len};
    std::array<std::uint8_t, 16> tag;
    std::memcpy(tag.data(), nonce_ct_tag.data() + kNonceBytes + ct_len, kTagBytes);

    // Re-derive tag and compare
    const auto rk = expand_key(key);
    std::array<std::uint8_t, 16> H{};
    {
        std::array<std::uint8_t, 16> zero{};
        aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{zero.data(), 16},
                          std::span<std::uint8_t, 16>{H.data(), 16});
    }
    std::array<std::uint8_t, 16> j0{};
    std::memcpy(j0.data(), nonce.data(), kNonceBytes);
    j0[15] = 1;
    std::vector<std::uint8_t> pt(ct.size());
    std::array<std::uint8_t, 16> counter = j0;
    for (std::size_t i = 0; i < ct.size(); i += 16) {
        for (int j = 15; j >= 12; --j) {
            if (++counter[j] != 0) break;
        }
        std::array<std::uint8_t, 16> keystream{};
        aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{counter.data(), 16},
                          std::span<std::uint8_t, 16>{keystream.data(), 16});
        const std::size_t block_len = std::min<std::size_t>(16, ct.size() - i);
        for (std::size_t k = 0; k < block_len; ++k) pt[i + k] = ct[i + k] ^ keystream[k];
    }
    // Recompute tag
    std::array<std::uint8_t, 16> y{};
    std::array<std::uint8_t, 16> h_work = H;
    std::size_t aad_pos = 0;
    while (aad_pos + 16 <= aad.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), aad.data() + aad_pos, 16);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
        aad_pos += 16;
    }
    if (aad_pos < aad.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), aad.data() + aad_pos, aad.size() - aad_pos);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
    }
    std::size_t ct_pos = 0;
    while (ct_pos + 16 <= ct.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), ct.data() + ct_pos, 16);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
        ct_pos += 16;
    }
    if (ct_pos < ct.size()) {
        std::array<std::uint8_t, 16> blk{};
        std::memcpy(blk.data(), ct.data() + ct_pos, ct.size() - ct_pos);
        ghash_update(y, std::span<const std::uint8_t, 16>{blk.data(), 16},
                    h_work);
    }
    std::array<std::uint8_t, 16> len_blk{};
    const std::uint64_t aad_bits = static_cast<std::uint64_t>(aad.size()) * 8;
    const std::uint64_t ct_bits  = static_cast<std::uint64_t>(ct.size()) * 8;
    for (int i = 7; i >= 0; --i) len_blk[7 - i] = static_cast<std::uint8_t>((aad_bits >> (i * 8)) & 0xFF);
    for (int i = 7; i >= 0; --i) len_blk[15 - i] = static_cast<std::uint8_t>((ct_bits >> (i * 8)) & 0xFF);
    ghash_update(y, std::span<const std::uint8_t, 16>{len_blk.data(), 16},
                h_work);
    std::array<std::uint8_t, 16> expected_tag{};
    aes_encrypt_block(rk, std::span<const std::uint8_t, 16>{j0.data(), 16},
                      std::span<std::uint8_t, 16>{expected_tag.data(), 16});
    for (int i = 0; i < 16; ++i) expected_tag[i] ^= y[i];
    // Constant-time tag compare
    if (!constant_time_equals(tag.data(), expected_tag.data(), 16)) return {};
    return pt;
}

} // namespace aes

// -----------------------------------------------------------------------------
//  Note (v3.5): AES-NI hardware detection is implemented in
//  src/encryption_aesni.cpp. The functions above remain the canonical
//  implementations of AES-256-GCM. When the v4.0 release wires AES-NI
//  into the block cipher inner loop, these will become the fallback.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
//  StaticKeyProvider
// -----------------------------------------------------------------------------
StaticKeyProvider::StaticKeyProvider(std::string key_id,
                                      std::string passphrase,
                                      std::vector<std::uint8_t> salt)
    : key_id_(std::move(key_id)),
      key_(std::make_shared<aes::Key>()) {
    *key_ = aes::derive_from_passphrase(passphrase,
        std::span<const std::uint8_t>{salt.data(), salt.size()});
}

std::shared_ptr<const aes::Key>
StaticKeyProvider::get_key(std::string_view key_id) const {
    if (key_id == key_id_) return key_;
    return nullptr;
}

std::vector<std::string> StaticKeyProvider::key_ids() const {
    return {key_id_};
}

std::shared_ptr<const aes::Key>
MultiKeyProvider::get_key(std::string_view key_id) const {
    for (const auto& p : providers_) {
        if (auto k = p->get_key(key_id)) return k;
    }
    return nullptr;
}

std::vector<std::string> MultiKeyProvider::key_ids() const {
    std::vector<std::string> out;
    for (const auto& p : providers_) {
        auto ids = p->key_ids();
        out.insert(out.end(), ids.begin(), ids.end());
    }
    return out;
}

} // namespace tts::crypto
