// Tests: TTF writer -> reader round-trip
#include <gtest/gtest.h>
#include "tts/ttf_reader.hpp"
#include "tts/ttf_writer.hpp"
#include "tts/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace tts;

TEST(TtfRoundTrip, BasicWriteRead) {
    TtfWriter w;
    w.set_tfd_id(1).set_next_tfd(0);
    w.add_section(1, "Hello world");
    w.add_section(2, "Second section");
    w.add_string_tag(256, "DocumentTitle", "Test Doc");

    const std::string bytes = w.build();
    ASSERT_FALSE(bytes.empty());

    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = TtfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value()) << tts::to_string(r.error());

    EXPECT_EQ(r->metadata().tfd_id, 1u);
    EXPECT_EQ(r->metadata().next_tfd, 0u);
    ASSERT_EQ(r->metadata().sections.size(), 2u);

    auto s1 = r->get_section(1);
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(*s1, std::string{"Hello world"});

    auto s2 = r->get_section(2);
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(*s2, std::string{"Second section"});
}

TEST(TtfRoundTrip, MultibyteUtf8) {
    TtfWriter w;
    w.add_section(1, "你好世界 🌍");
    w.add_section(2, "héllo wörld");

    const std::string bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = TtfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());

    auto s1 = r->get_section(1);
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(*s1, std::string{"你好世界 🌍"});

    auto s2 = r->get_section(2);
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(*s2, std::string{"héllo wörld"});
}

TEST(TtfRoundTrip, TamperDetection) {
    TtfWriter w;
    w.add_section(1, "original content");

    std::string bytes = w.build();
    // Tamper with the data stream: replace 'o' with 'X'.
    auto pos = bytes.find("original");
    ASSERT_NE(pos, std::string::npos);
    bytes[pos] = 'X';

    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = TtfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());

    auto s = r->get_section(1);
    ASSERT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), TtfError::HashMismatch);
}

TEST(TtfRoundTrip, FileBackedRoundTrip) {
    const auto tmp = std::filesystem::temp_directory_path() / "tts_test_roundtrip.ttf";
    {
        TtfWriter w;
        w.add_section(1, "file-backed content");
        w.add_section(2, "second file section");
        auto st = w.write_to_file(tmp);
        ASSERT_TRUE(st.has_value()) << tts::to_string(st.error());
    }
    {
        auto r = TtfReader::open_file(tmp);
        ASSERT_TRUE(r.has_value()) << tts::to_string(r.error());
        auto s1 = r->get_section(1);
        ASSERT_TRUE(s1.has_value());
        EXPECT_EQ(*s1, std::string{"file-backed content"});
        auto s2 = r->get_section(2);
        ASSERT_TRUE(s2.has_value());
        EXPECT_EQ(*s2, std::string{"second file section"});
    }
    std::filesystem::remove(tmp);
}

TEST(TtfRoundTrip, ValidateAllPasses) {
    TtfWriter w;
    w.add_section(1, "alpha");
    w.add_section(2, "beta");
    w.add_section(3, "gamma");
    auto bytes = w.build();
    auto mv = MemoryView::own(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    auto r = TtfReader::open(std::move(mv));
    ASSERT_TRUE(r.has_value());
    auto v = r->validate_all();
    EXPECT_TRUE(v.has_value());
}
