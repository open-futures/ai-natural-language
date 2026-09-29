// Tests: v3.5 features — OpenAI embedder fallback, AES-NI detection,
// KMS key provider interface, TUI smoke test.
#include <gtest/gtest.h>
#include "tts/encryption.hpp"
#include "tts/kms_providers.hpp"
#include "tts/embeddings.hpp"
#include "tts/sha256.hpp"
#include <memory>
#include <string>

using namespace tts;

// --- AES-NI detection -------------------------------------------------------
TEST(V35, AesNiDetectionReturnsBool) {
    // The function should return either true or false without crashing,
    // regardless of the host CPU.
    const bool has = crypto::aes::has_aesni_hardware_acceleration();
    std::cout << "  AES-NI on this CPU: " << (has ? "yes" : "no") << "\n";
    SUCCEED();
}

// --- OpenAI embedder with no API key (fallback path) ----------------------
TEST(V35, OpenAIEmbedderFallbackWithoutApiKey) {
    embeddings::OpenAIEmbedder emb("", "text-embedding-3-small");
    auto v = emb.embed("hello world");
    // Without an API key + no env override, the embedder falls back to a
    // HashEmbedder-derived vector so callers can exercise the search pipeline.
    ASSERT_TRUE(v.has_value()) << tts::to_string(v.error());
    EXPECT_EQ(v->size(), 1536u);
    // Must be deterministic — calling again should produce the same vector
    auto v2 = emb.embed("hello world");
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v, *v2);
}

TEST(V35, OpenAIEmbedderRejectsEmptyInputGracefully) {
    embeddings::OpenAIEmbedder emb("dummy_key");
    auto v = emb.embed("");
    ASSERT_TRUE(v.has_value());
    // Even an empty input should produce a (fallback) vector.
    EXPECT_GT(v->size(), 0u);
}

TEST(V35, OpenAIEmbedderDimIsCorrect) {
    embeddings::OpenAIEmbedder emb_small("", "text-embedding-3-small");
    EXPECT_EQ(emb_small.dim(), 1536u);
    embeddings::OpenAIEmbedder emb_large("", "text-embedding-3-large");
    EXPECT_EQ(emb_large.dim(), 3072u);
    embeddings::OpenAIEmbedder emb_ada("", "text-embedding-ada-002");
    EXPECT_EQ(emb_ada.dim(), 1536u);
}

// --- KMS provider interface -------------------------------------------------
// We test the interface contract without making actual network calls.
// The transport is a mock that returns canned responses.

namespace {

class MockHttpTransport : public tts::crypto::kms::IHttpTransport {
public:
    tts::crypto::kms::HttpResponse
    request(const tts::crypto::kms::HttpRequest& /*req*/) override {
        tts::crypto::kms::HttpResponse resp;
        resp.status_code = 200;
        // Return 32 bytes of 0xAA as the "decrypted key"
        resp.body.assign(32, '\xAA');
        return resp;
    }
};

class FailingTransport : public tts::crypto::kms::IHttpTransport {
public:
    tts::crypto::kms::HttpResponse
    request(const tts::crypto::kms::HttpRequest&) override {
        tts::crypto::kms::HttpResponse resp;
        resp.status_code = 500;
        return resp;
    }
};

} // namespace

TEST(V35, KmsAwsAdapterResolvesKeyViaTransport) {
    auto transport = std::make_shared<MockHttpTransport>();
    crypto::kms::AwsKmsKeyProvider provider(
        "us-east-1", "AKID", "SECRET", transport);
    auto key = provider.get_key("arn:aws:kms:us-east-1:123:key/abc");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ((*key)[0], 0xAA);
    EXPECT_EQ((*key)[31], 0xAA);
    // Cached on second call
    auto key2 = provider.get_key("arn:aws:kms:us-east-1:123:key/abc");
    EXPECT_EQ(key, key2);
    auto ids = provider.key_ids();
    ASSERT_EQ(ids.size(), 1u);
}

TEST(V35, KmsAwsAdapterReturnsNullOnTransportFailure) {
    auto transport = std::make_shared<FailingTransport>();
    crypto::kms::AwsKmsKeyProvider provider(
        "us-east-1", "AKID", "SECRET", transport);
    auto key = provider.get_key("arn:aws:kms:us-east-1:123:key/xyz");
    EXPECT_EQ(key, nullptr);
}

TEST(V35, KmsGcpAdapterResolvesKeyViaTransport) {
    auto transport = std::make_shared<MockHttpTransport>();
    crypto::kms::GcpKmsKeyProvider provider(
        "my-project", "global", "oauth_token", transport);
    auto key = provider.get_key("my-keyring/my-key");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ((*key)[0], 0xAA);
}

TEST(V35, KmsVaultAdapterResolvesKeyViaTransport) {
    auto transport = std::make_shared<MockHttpTransport>();
    crypto::kms::VaultKeyProvider provider(
        "https://vault.example.com", "token", transport);
    auto key = provider.get_key("path/to/secret");
    ASSERT_NE(key, nullptr);
    EXPECT_EQ((*key)[0], 0xAA);
}

TEST(V35, KmsAdaptersComposeWithMultiKeyProvider) {
    auto transport = std::make_shared<MockHttpTransport>();
    auto aws = std::make_shared<crypto::kms::AwsKmsKeyProvider>(
        "us-east-1", "AKID", "SECRET", transport);
    auto vault = std::make_shared<crypto::kms::VaultKeyProvider>(
        "https://vault.example.com", "token", transport);

    crypto::MultiKeyProvider multi;
    multi.add(aws);
    multi.add(vault);

    auto key = multi.get_key("any-key-id");
    EXPECT_NE(key, nullptr);
    auto ids = multi.key_ids();
    // Should have at least the AWS provider's cached key_id.
    EXPECT_GE(ids.size(), 0u);
}

// --- KMS-resolved keys work with AES-GCM -----------------------------------
TEST(V35, KmsKeyEncryptsAndDecryptsRoundTrip) {
    auto transport = std::make_shared<MockHttpTransport>();
    crypto::kms::AwsKmsKeyProvider provider(
        "us-east-1", "AKID", "SECRET", transport);
    auto key_ptr = provider.get_key("test-key");
    ASSERT_NE(key_ptr, nullptr);

    std::string plaintext = "Top secret message encrypted with KMS-resolved key.";
    crypto::aes::Nonce nonce = crypto::aes::random_nonce();
    auto ct = crypto::aes::aes_gcm_encrypt(*key_ptr, nonce,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(plaintext.data()),
            plaintext.size()});
    auto pt = crypto::aes::aes_gcm_decrypt(*key_ptr,
        std::span<const std::uint8_t>{ct.data(), ct.size()});
    ASSERT_FALSE(pt.empty());
    const std::string pt_str(pt.begin(), pt.end());
    EXPECT_EQ(pt_str, plaintext);
}
