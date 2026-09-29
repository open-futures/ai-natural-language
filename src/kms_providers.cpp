// =============================================================================
//  src/kms_providers.cpp — v3.5
//
//  Optional KMS key provider implementations. Always compiled as a TU, but
//  the actual KMS-callable methods are stubs unless the corresponding
//  CMake option is ON. This keeps the SDK linkable without KMS deps while
//  still giving production users the pattern.
// =============================================================================
#include "tts/kms_providers.hpp"

#include <cstring>
#include <sstream>

namespace tts::crypto::kms {

// -----------------------------------------------------------------------------
//  AwsKmsKeyProvider
// -----------------------------------------------------------------------------
AwsKmsKeyProvider::AwsKmsKeyProvider(std::string region,
                                       std::string access_key_id,
                                       std::string secret_access_key,
                                       std::shared_ptr<IHttpTransport> transport)
    : region_(std::move(region)),
      access_key_id_(std::move(access_key_id)),
      secret_access_key_(std::move(secret_access_key)),
      transport_(std::move(transport)) {}

std::shared_ptr<const aes::Key>
AwsKmsKeyProvider::get_key(std::string_view key_id) const {
    if (cached_key_ && cached_key_id_ == key_id) return cached_key_;
    if (!transport_) return nullptr;

    // POST https://kms.<region>.amazonaws.com/ - SigV4 signed
    // For the reference implementation we just send an unsigned request;
    // production callers wrap transport_ with a SigV4-signing decorator.
    HttpRequest req;
    req.method = "POST";
    req.url = "https://kms." + region_ + ".amazonaws.com/";
    req.headers.emplace_back("Content-Type", "application/x-amz-json-1.1");
    req.headers.emplace_back("X-Amz-Target", "TrentService.Decrypt");
    std::ostringstream body;
    body << "{\"CiphertextBlob\":\"" << key_id << "\"}";
    req.body = body.str();

    HttpResponse resp = transport_->request(req);
    if (resp.status_code != 200 || resp.body.size() < 32) return nullptr;

    auto key = std::make_shared<aes::Key>();
    // The KMS Decrypt response returns the plaintext key bytes base64-encoded.
    // For the reference, we take the first 32 bytes of the body as the key.
    // Real implementations should base64-decode + JSON-parse the response.
    std::memcpy(key->data(), resp.body.data(), 32);
    cached_key_ = key;
    cached_key_id_ = std::string{key_id};
    return key;
}

std::vector<std::string> AwsKmsKeyProvider::key_ids() const {
    return {cached_key_id_};
}

// -----------------------------------------------------------------------------
//  GcpKmsKeyProvider
// -----------------------------------------------------------------------------
GcpKmsKeyProvider::GcpKmsKeyProvider(std::string project,
                                       std::string location,
                                       std::string oauth_token,
                                       std::shared_ptr<IHttpTransport> transport)
    : project_(std::move(project)),
      location_(std::move(location)),
      oauth_token_(std::move(oauth_token)),
      transport_(std::move(transport)) {}

std::shared_ptr<const aes::Key>
GcpKmsKeyProvider::get_key(std::string_view key_id) const {
    if (cached_key_ && cached_key_id_ == key_id) return cached_key_;
    if (!transport_) return nullptr;

    HttpRequest req;
    req.method = "POST";
    req.url = "https://cloudkms.googleapis.com/v1/projects/" + project_ +
              "/locations/" + location_ + "/keyRings/" + std::string{key_id} +
              "/cryptoKeys:decrypt";
    req.headers.emplace_back("Authorization", "Bearer " + oauth_token_);
    req.headers.emplace_back("Content-Type", "application/json");
    req.body = "{}";

    HttpResponse resp = transport_->request(req);
    if (resp.status_code != 200 || resp.body.size() < 32) return nullptr;

    auto key = std::make_shared<aes::Key>();
    std::memcpy(key->data(), resp.body.data(), 32);
    cached_key_ = key;
    cached_key_id_ = std::string{key_id};
    return key;
}

std::vector<std::string> GcpKmsKeyProvider::key_ids() const {
    return {cached_key_id_};
}

// -----------------------------------------------------------------------------
//  VaultKeyProvider
// -----------------------------------------------------------------------------
VaultKeyProvider::VaultKeyProvider(std::string vault_url,
                                     std::string token,
                                     std::shared_ptr<IHttpTransport> transport)
    : vault_url_(std::move(vault_url)),
      token_(std::move(token)),
      transport_(std::move(transport)) {}

std::shared_ptr<const aes::Key>
VaultKeyProvider::get_key(std::string_view key_id) const {
    if (cached_key_ && cached_key_id_ == key_id) return cached_key_;
    if (!transport_) return nullptr;

    HttpRequest req;
    req.method = "GET";
    req.url = vault_url_ + "/v1/secret/data/" + std::string{key_id};
    req.headers.emplace_back("X-Vault-Token", token_);

    HttpResponse resp = transport_->request(req);
    if (resp.status_code != 200 || resp.body.size() < 32) return nullptr;

    auto key = std::make_shared<aes::Key>();
    std::memcpy(key->data(), resp.body.data(), 32);
    cached_key_ = key;
    cached_key_id_ = std::string{key_id};
    return key;
}

std::vector<std::string> VaultKeyProvider::key_ids() const {
    return {cached_key_id_};
}

} // namespace tts::crypto::kms
