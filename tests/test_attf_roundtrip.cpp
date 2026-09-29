// Tests: ATTF writer -> reader round-trip
#include <gtest/gtest.h>
#include "tts/attf_reader.hpp"
#include "tts/attf_writer.hpp"
#include <string>
#include <vector>

using namespace tts;

TEST(AttfRoundTrip, BasicWriteRead) {
    AttfWriter w;
    w.set_version("2.0");
    w.add_setting("mem_zone", "active");
    w.add_setting("last_sync", "2026-07-11");

    w.add_page(1, "sys_prompt", "0x00A1", 9.9,
               "You are a kernel optimization agent.");
    w.add_page(2, "episodic", "0x0BF2", 8.5,
               "User asked about memory allocators.");

    const std::string bytes = w.build();
    ASSERT_FALSE(bytes.empty());

    // Sanity: must start with \x01 and contain \x02 (STX).
    EXPECT_EQ(static_cast<unsigned char>(bytes[0]), 0x01u);
    EXPECT_NE(bytes.find('\x02'), std::string::npos);

    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value()) << tts::to_string(r.error());

    EXPECT_EQ(r->header().version, "2.0");
    EXPECT_EQ(r->header().get_setting("mem_zone"), "active");
    EXPECT_EQ(r->header().get_setting("last_sync"), "2026-07-11");
    ASSERT_EQ(r->directory().size(), 2u);
    EXPECT_EQ(r->directory()[0].hex_address, "0x00A1");
    EXPECT_EQ(r->directory()[1].hex_address, "0x0BF2");

    auto p1 = r->get_page("0x00A1");
    ASSERT_TRUE(p1.has_value()) << tts::to_string(p1.error());
    EXPECT_EQ(*p1, std::string{"You are a kernel optimization agent."});

    auto p2 = r->get_page("0x0BF2");
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(*p2, std::string{"User asked about memory allocators."});
}

TEST(AttfRoundTrip, WithKeywords) {
    AttfWriter w;
    w.add_page(1, "semantic", "0x5C11", 9.5,
               "OAuth2 tokens must expire after 900 seconds.",
               {"auth", "oauth2", "security"});

    const std::string bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->semantic_index().size(), 1u);
    EXPECT_EQ(r->semantic_index()[0].hex_address, "0x5C11");
    ASSERT_EQ(r->semantic_index()[0].keywords.size(), 3u);
    EXPECT_EQ(r->semantic_index()[0].keywords[0], "auth");
    EXPECT_EQ(r->semantic_index()[0].keywords[1], "oauth2");
    EXPECT_EQ(r->semantic_index()[0].keywords[2], "security");
}

TEST(AttfRoundTrip, GetById) {
    AttfWriter w;
    w.add_page(42, "episodic", "0xDEAD", 5.0, "event at id 42");

    const std::string bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());

    auto p = r->get_page_by_id(42);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(*p, std::string{"event at id 42"});

    auto missing = r->get_page_by_id(99);
    EXPECT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error(), AttfError::PageNotFound);
}

TEST(AttfRoundTrip, ZeroCopyAliasing) {
    AttfWriter w;
    w.add_page(1, "sys_prompt", "0x1000", 9.0, "verbatim content");
    const std::string bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    const auto* buf_base = mv.bytes().data();
    auto r = AttfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    auto p = r->get_page("0x1000");
    ASSERT_TRUE(p.has_value());
    // The string_view must point into the original buffer (zero-copy).
    EXPECT_GE(p->data(), reinterpret_cast<const char*>(buf_base));
    EXPECT_LT(p->data(), reinterpret_cast<const char*>(buf_base + mv.bytes().size()));
}
