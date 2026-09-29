// Tests: v3.0 features — constant-time compare, ATTF escapes, compression,
// encryption, next_tfd, semantic search.
#include <gtest/gtest.h>
#include "tts/sha256.hpp"
#include "tts/compression.hpp"
#include "tts/encryption.hpp"
#include "tts/attf_reader.hpp"
#include "tts/attf_writer.hpp"
#include "tts/embeddings.hpp"
#include "tts/semantic_search.hpp"
#include "tts/streaming_writer.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/ttf_writer.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace tts;

// --- Constant-time comparison ----------------------------------------------
TEST(V3, ConstantTimeEqualsEqual) {
    std::string a = "deadbeef";
    std::string b = "deadbeef";
    EXPECT_TRUE(crypto::constant_time_equals_hex(a, b));
}

TEST(V3, ConstantTimeEqualsDifferent) {
    std::string a = "deadbeef";
    std::string b = "deadbe00";
    EXPECT_FALSE(crypto::constant_time_equals_hex(a, b));
}

TEST(V3, ConstantTimeEqualsDifferentLength) {
    std::string a = "deadbeef";
    std::string b = "deadbeef00";
    EXPECT_FALSE(crypto::constant_time_equals_hex(a, b));
}

// --- ATTF escape sequences -------------------------------------------------
TEST(V3, AttfEscapePreservesBinaryContent) {
    // Content with embedded \x03 and \x1b bytes
    std::string content = "binary data \x03 inside \x1b text";
    AttfWriter w;
    w.add_page(1, "binary", "0x0001", 5.0, content);
    auto bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value()) << tts::to_string(r.error());
    auto page = r->get_page("0x0001");
    ASSERT_TRUE(page.has_value()) << tts::to_string(page.error());
    EXPECT_EQ(*page, content);
}

TEST(V3, AttfEscapePreservesRegularContent) {
    // Normal text — should round-trip unchanged
    std::string content = "This is regular text with no escape bytes.";
    AttfWriter w;
    w.add_page(1, "text", "0x0001", 5.0, content);
    auto bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    auto page = r->get_page("0x0001");
    ASSERT_TRUE(page.has_value());
    EXPECT_EQ(*page, content);
}

// --- Compression ------------------------------------------------------------
TEST(V3, CompressionRoundTrip) {
    std::string original(1000, 'x');
    auto compressed = compression::compress_string(compression::Algorithm::Zstd, original);
    // zstd may not be built in; skip if so.
    if (!compressed) {
        GTEST_SKIP() << "zstd not built in: " << tts::to_string(compressed.error());
    }
    auto decompressed = compression::decompress_string(compression::Algorithm::Zstd, *compressed);
    ASSERT_TRUE(decompressed.has_value());
    EXPECT_EQ(*decompressed, original);
}

// --- Encryption -------------------------------------------------------------
TEST(V3, AesGcmRoundTrip) {
    std::string plaintext = "The quick brown fox jumps over the lazy dog.";
    crypto::aes::Key key{};  // all-zero key for test
    crypto::aes::Nonce nonce = crypto::aes::random_nonce();
    auto ct = crypto::aes::aes_gcm_encrypt(key, nonce,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size()});
    auto pt = crypto::aes::aes_gcm_decrypt(key,
        std::span<const std::uint8_t>{ct.data(), ct.size()});
    ASSERT_FALSE(pt.empty());
    const std::string pt_str(pt.begin(), pt.end());
    EXPECT_EQ(pt_str, plaintext);
}

TEST(V3, AesGcmTamperedTagFails) {
    std::string plaintext = "secret message";
    crypto::aes::Key key{};
    crypto::aes::Nonce nonce = crypto::aes::random_nonce();
    auto ct = crypto::aes::aes_gcm_encrypt(key, nonce,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size()});
    // Tamper with the tag (last byte)
    ct.back() ^= 0x01;
    auto pt = crypto::aes::aes_gcm_decrypt(key,
        std::span<const std::uint8_t>{ct.data(), ct.size()});
    EXPECT_TRUE(pt.empty());  // decryption fails — tag mismatch
}

TEST(V3, StaticKeyProviderLookup) {
    crypto::StaticKeyProvider p("default", "my_passphrase");
    auto k = p.get_key("default");
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(p.get_key("nonexistent"), nullptr);
    auto ids = p.key_ids();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], "default");
}

// --- Hash embedder + semantic search ---------------------------------------
TEST(V3, HashEmbedderProducesStableVectors) {
    embeddings::HashEmbedder e(256);
    auto v1 = e.embed("hello");
    auto v2 = e.embed("hello");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(v1->size(), 256u);
    EXPECT_EQ(*v1, *v2);
    auto v3 = e.embed("world");
    EXPECT_NE(*v1, *v3);  // different inputs → different vectors
}

TEST(V3, HashEmbedderIsUnitNorm) {
    embeddings::HashEmbedder e(128);
    auto v = e.embed("anything");
    ASSERT_TRUE(v.has_value());
    float sum_sq = 0.0f;
    for (float x : *v) sum_sq += x * x;
    EXPECT_NEAR(sum_sq, 1.0f, 0.01f);
}

TEST(V3, SemanticSearchFindsPages) {
    // Build an ATTF with two pages, one of which has an embedding prefix.
    // Use std::string constructor with explicit length to preserve embedded
    // NUL bytes (C++ string literals are NUL-terminated).
    AttfWriter w;
    w.add_setting("mem_zone", "active");
    std::string emb1 = std::string{"\x00""EMB:0.5,0.5,0.5,0.5\nThe first embedded page.", 47};
    std::string emb2 = std::string{"\x00""EMB:0.5,0.4,0.3,0.2\nThe second page.", 35};
    w.add_page(1, "semantic", "0x0001", 9.0, emb1, {"alpha"});
    w.add_page(2, "semantic", "0x0002", 8.0, emb2, {"beta"});
    auto bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    auto idx = semantic::parse_index(*r);
    EXPECT_EQ(idx.entries.size(), 2u);
    EXPECT_EQ(idx.entries[0].hex_address, "0x0001");
    EXPECT_EQ(idx.entries[0].vector.size(), 4u);

    // Search for something close to page 1
    std::vector<float> query = {0.5f, 0.5f, 0.5f, 0.5f};
    semantic::SearchOpts opts;
    opts.top_k = 2;
    auto results = semantic::search_flat(idx,
        std::span<const float>{query.data(), 4}, *r, opts);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].hex_address, "0x0001");  // closest match
}

TEST(V3, HybridSearchCombinesSignals) {
    AttfWriter w;
    w.add_setting("mem_zone", "active");
    std::string emb1 = std::string{"\x00""EMB:0.5,0.5\nKernel optimization page.", 36};
    std::string emb2 = std::string{"\x00""EMB:0.4,0.4\nMemory allocator page.", 35};
    w.add_page(1, "semantic", "0x0001", 9.0, emb1, {"kernel", "optimization"});
    w.add_page(2, "semantic", "0x0002", 8.0, emb2, {"memory", "alloc"});
    auto bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    auto idx = semantic::parse_index(*r);
    std::vector<float> query = {0.5f, 0.5f};
    semantic::SearchOpts opts;
    opts.top_k = 2;
    auto results = semantic::search_hybrid(*r, idx,
        std::span<const float>{query.data(), 2},
        "kernel optimization", 0.3f, 0.7f, opts);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].hex_address, "0x0001");  // keyword + semantic match
}

// --- next_tfd chain ---------------------------------------------------------
TEST(V3, NextTfdChainTraversal) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path();
    auto p1 = tmp / "v3_chain1.ttf";
    auto p2 = tmp / "v3_chain1.ttf.next.ttf";

    // First file: section 1, next_tfd=2
    {
        TtfWriter w;
        w.set_tfd_id(1).set_next_tfd(2);
        w.add_section(1, "section in file 1");
        (void)w.write_to_file(p1);
    }
    // Second file: section 2
    {
        TtfWriter w;
        w.set_tfd_id(2).set_next_tfd(0);
        w.add_section(2, "section in file 2");
        (void)w.write_to_file(p2);
    }
    // Open first, follow chain
    auto r1 = TtfReader::open_file(p1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->metadata().next_tfd, 2u);
    auto chain = r1->follow_chain(p1);
    ASSERT_TRUE(chain.has_value());
    ASSERT_EQ(chain->size(), 1u);
    // Section 2 should be findable through the chain
    auto s2 = r1->get_section_in_chain(2, *chain);
    ASSERT_TRUE(s2.has_value()) << tts::to_string(s2.error());
    EXPECT_EQ(*s2, std::string{"section in file 2"});

    fs::remove(p1);
    fs::remove(p2);
}

// --- Streaming writer -------------------------------------------------------
TEST(V3, StreamingWriterProducesValidTtf) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "v3_streaming.ttf";

    StreamingTtfWriter sw;
    sw.set_tfd_id(1).set_next_tfd(0);
    sw.add_section(1, "streaming section 1");
    sw.add_section(2, "streaming section 2");
    auto st = sw.write_to_file(tmp);
    ASSERT_TRUE(st.has_value()) << tts::to_string(st.error());

    auto r = TtfReader::open_file(tmp);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->metadata().sections.size(), 2u);
    auto s1 = r->get_section(1);
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(*s1, std::string{"streaming section 1"});
    auto s2 = r->get_section(2);
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(*s2, std::string{"streaming section 2"});
    fs::remove(tmp);
}
