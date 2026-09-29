// =============================================================================
//  include/tts/encryption.hpp
//  Phase M2 — Per-section encryption interface.
//
//  Spec (TTF v3.0): SectionLayout gains optional `encryption` + `key_id`:
//    - section: 1
//      ...
//      encryption: "aes-256-gcm"
//      key_id:     "default"
//
//  SHA-256 is computed over the PLAINTEXT (before encryption). The data
//  stream stores: nonce(12 bytes) || ciphertext || tag(16 bytes).
//
//  Key management is delegated to an IKeyProvider interface. Two built-in
//  providers ship:
//
//    StaticKeyProvider  — derives a key from a passphrase via Argon2id-lite
//                         (PBKDF2-HMAC-SHA256 with 200k iterations for the
//                         shipped reference implementation; production users
//                         should swap in libsodium or Argon2 proper).
//
//    KmsKeyProvider     — abstract base; AWS KMS / GCP KMS / Vault adapters
//                         are separate packages that subclass this.
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tts::crypto {

// -----------------------------------------------------------------------------
//  AES-256-GCM — minimal in-tree implementation.
//
//  We vendor AES + GHASH here so the SDK can ship without OpenSSL / Crypto++
//  for the encryption path. The implementation is constant-time-ish on the
//  AES core (no S-box lookup-table-dependent timing on the hot path), but
//  NOT hardened against cache-based side-channels. For production:
//  - Wrap with a hardware AES-NI path, OR
//  - Swap in libsodium via build option -DTTS_USE_SODIUM=ON (roadmap).
// -----------------------------------------------------------------------------
namespace aes {

constexpr std::size_t kKeyBytes   = 32;   // AES-256
constexpr std::size_t kNonceBytes = 12;   // GCM standard
constexpr std::size_t kTagBytes   = 16;

using Key   = std::array<std::uint8_t, kKeyBytes>;
using Nonce = std::array<std::uint8_t, kNonceBytes>;
using Tag   = std::array<std::uint8_t, kTagBytes>;

[[nodiscard]] Key derive_from_passphrase(std::string_view passphrase,
                                          std::span<const std::uint8_t> salt,
                                          std::uint32_t iterations = 200'000);

[[nodiscard]] Nonce random_nonce();

/// v3.5: detect whether AES-NI hardware acceleration is available on the
/// current CPU. The current build routes through the portable C path; full
/// AES-NI integration is on the v4.0 roadmap. This function lets callers
/// plan capacity and performance budgets.
[[nodiscard]] bool has_aesni_hardware_acceleration() noexcept;

/// Encrypts `plaintext` with `key` and `nonce`. Returns nonce || ciphertext || tag.
[[nodiscard]] std::vector<std::uint8_t>
aes_gcm_encrypt(const Key& key, const Nonce& nonce,
                std::span<const std::uint8_t> plaintext,
                std::span<const std::uint8_t> aad = {});

/// Decrypts `nonce || ciphertext || tag` (as produced by aes_gcm_encrypt).
/// Returns the plaintext on success, or an empty optional on auth failure.
[[nodiscard]] std::vector<std::uint8_t>
aes_gcm_decrypt(const Key& key,
                std::span<const std::uint8_t> nonce_ct_tag,
                std::span<const std::uint8_t> aad = {});

} // namespace aes

// -----------------------------------------------------------------------------
//  Key provider interface
// -----------------------------------------------------------------------------
class IKeyProvider {
public:
    virtual ~IKeyProvider() = default;

    /// Look up the key for a given key_id. Returns nullptr if unknown.
    [[nodiscard]] virtual std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const = 0;

    /// List of key_ids this provider can resolve.
    [[nodiscard]] virtual std::vector<std::string> key_ids() const = 0;
};

// -----------------------------------------------------------------------------
//  StaticKeyProvider — single passphrase-derived key.
// -----------------------------------------------------------------------------
class StaticKeyProvider : public IKeyProvider {
public:
    /// Derives a key from `passphrase` and registers it under `key_id`.
    explicit StaticKeyProvider(std::string key_id,
                                std::string passphrase,
                                std::vector<std::uint8_t> salt = {});

    [[nodiscard]] std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const override;

    [[nodiscard]] std::vector<std::string> key_ids() const override;

private:
    std::string key_id_;
    std::shared_ptr<aes::Key> key_;
};

// -----------------------------------------------------------------------------
//  MultiKeyProvider — composes multiple providers, tried in order.
// -----------------------------------------------------------------------------
class MultiKeyProvider : public IKeyProvider {
public:
    void add(std::shared_ptr<IKeyProvider> p) { providers_.push_back(std::move(p)); }

    [[nodiscard]] std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const override;

    [[nodiscard]] std::vector<std::string> key_ids() const override;

private:
    std::vector<std::shared_ptr<IKeyProvider>> providers_;
};

} // namespace tts::crypto
