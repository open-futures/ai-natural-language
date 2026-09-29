// =============================================================================
//  include/tts/kms_providers.hpp — v3.5
//
//  Optional KMS-backed key providers. These are NOT compiled by default —
//  they're gated behind CMake options:
//    -DTTS_USE_AWS_KMS=ON
//    -DTTS_USE_GCP_KMS=ON
//    -DTTS_USE_VAULT=ON
//
//  Philosophy #6: "The dependency surface is the API's portability surface."
//  Adding hard deps on the AWS SDK / GCP SDK / Vault client would break
//  the SDK's portability. Instead, these adapters:
//    1. Implement IKeyProvider
//    2. Are conditionally compiled only when the corresponding CMake
//       option is ON
//    3. Resolve keys via the caller-supplied HTTP transport (the caller
//       passes an IHttpTransport*, so no AWS/GCP SDK is required)
//
//  This keeps the core SDK dependency-free while giving production users
//  a clear pattern for integrating with their KMS of choice.
// =============================================================================
#pragma once

#include "tts/encryption.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tts::crypto::kms {

// -----------------------------------------------------------------------------
//  HTTP transport interface — the caller supplies the implementation.
//
//  This is the philosophy-aligned way to integrate with KMS without
//  pulling in AWS SDK / GCP SDK / Vault client as hard deps.
// -----------------------------------------------------------------------------
struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct HttpResponse {
    int         status_code = 0;
    std::string body;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    [[nodiscard]] virtual HttpResponse request(const HttpRequest& req) = 0;
};

// -----------------------------------------------------------------------------
//  AWS KMS adapter (compiled when TTS_USE_AWS_KMS=ON)
//
//  Resolves a key_id (the KMS key UUID) into a 32-byte AES key by calling
//  the KMS Decrypt API. The caller must supply:
//    - an IHttpTransport that signs requests with AWS SigV4
//    - the AWS region (e.g. "us-east-1")
//
//  The caller is responsible for IAM credentials — we don't read them.
// -----------------------------------------------------------------------------
class AwsKmsKeyProvider : public IKeyProvider {
public:
    AwsKmsKeyProvider(std::string region,
                       std::string access_key_id,
                       std::string secret_access_key,
                       std::shared_ptr<IHttpTransport> transport);

    [[nodiscard]] std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const override;

    [[nodiscard]] std::vector<std::string> key_ids() const override;

private:
    std::string region_;
    std::string access_key_id_;
    std::string secret_access_key_;
    std::shared_ptr<IHttpTransport> transport_;
    mutable std::shared_ptr<aes::Key> cached_key_;
    mutable std::string cached_key_id_;
};

// -----------------------------------------------------------------------------
//  GCP KMS adapter (compiled when TTS_USE_GCP_KMS=ON)
//
//  Resolves a key_id (the GCP KMS key path) into a 32-byte AES key.
//  The caller must supply:
//    - an IHttpTransport that adds the GCP OAuth Bearer token
//    - the GCP project + location
// -----------------------------------------------------------------------------
class GcpKmsKeyProvider : public IKeyProvider {
public:
    GcpKmsKeyProvider(std::string project,
                       std::string location,
                       std::string oauth_token,
                       std::shared_ptr<IHttpTransport> transport);

    [[nodiscard]] std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const override;

    [[nodiscard]] std::vector<std::string> key_ids() const override;

private:
    std::string project_;
    std::string location_;
    std::string oauth_token_;
    std::shared_ptr<IHttpTransport> transport_;
    mutable std::shared_ptr<aes::Key> cached_key_;
    mutable std::string cached_key_id_;
};

// -----------------------------------------------------------------------------
//  HashiCorp Vault adapter (compiled when TTS_USE_VAULT=ON)
//
//  Resolves a key_id (the Vault secret path) into a 32-byte AES key.
//  The caller must supply:
//    - an IHttpTransport that adds the Vault token header
//    - the Vault server URL
// -----------------------------------------------------------------------------
class VaultKeyProvider : public IKeyProvider {
public:
    VaultKeyProvider(std::string vault_url,
                     std::string token,
                     std::shared_ptr<IHttpTransport> transport);

    [[nodiscard]] std::shared_ptr<const aes::Key>
    get_key(std::string_view key_id) const override;

    [[nodiscard]] std::vector<std::string> key_ids() const override;

private:
    std::string vault_url_;
    std::string token_;
    std::shared_ptr<IHttpTransport> transport_;
    mutable std::shared_ptr<aes::Key> cached_key_;
    mutable std::string cached_key_id_;
};

} // namespace tts::crypto::kms
