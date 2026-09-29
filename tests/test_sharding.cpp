// Tests: ShardedReader (M7)
#include <gtest/gtest.h>
#include "tts/sharded_reader.hpp"
#include "tts/ttf_writer.hpp"
#include <filesystem>

using namespace tts;

TEST(ShardedReader, WalksChain) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path();
    auto p1 = tmp / "v3_shard1.ttf";
    auto p2 = tmp / "v3_shard1.ttf.next.ttf";

    {
        TtfWriter w;
        w.set_tfd_id(1).set_next_tfd(2);
        w.add_section(1, "shard 1 section");
        w.write_to_file(p1);
    }
    {
        TtfWriter w;
        w.set_tfd_id(2).set_next_tfd(0);
        w.add_section(2, "shard 2 section");
        w.write_to_file(p2);
    }

    auto sr = ShardedReader::open(p1);
    ASSERT_TRUE(sr.has_value()) << tts::to_string(sr.error());
    EXPECT_EQ(sr->shard_count(), 2u);

    // Section 1 in shard 0, section 2 in shard 1
    auto s1 = sr->get_section(1);
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(*s1, std::string{"shard 1 section"});
    auto s2 = sr->get_section(2);
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(*s2, std::string{"shard 2 section"});

    auto s_missing = sr->get_section(999);
    EXPECT_FALSE(s_missing.has_value());

    auto ids = sr->section_ids();
    EXPECT_EQ(ids.size(), 2u);

    fs::remove(p1);
    fs::remove(p2);
}
