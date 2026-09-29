// Tests: tts::agent::Memory (M5)
#include <gtest/gtest.h>
#include "tts/agent_memory.hpp"
#include "tts/embeddings.hpp"
#include <filesystem>

using namespace tts;

TEST(AgentMemory, RememberAndRecall) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "v3_agent_mem.attf";
    fs::remove(tmp);

    auto mem_r = agent::Memory::open(tmp);
    ASSERT_TRUE(mem_r.has_value()) << tts::to_string(mem_r.error());
    auto& mem = *mem_r;

    auto r1 = mem.remember("You are a kernel optimization agent.",
                            {.type = "sys_prompt", .priority = 9.9,
                             .keywords = {"kernel", "agent"}});
    ASSERT_TRUE(r1.has_value()) << tts::to_string(r1.error());
    EXPECT_EQ(r1->page_id, 1u);

    auto r2 = mem.remember("User asked about memory allocators.",
                            {.type = "episodic", .priority = 8.0,
                             .keywords = {"memory", "alloc"}});
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->page_id, 2u);

    auto r3 = mem.remember("Kernel security best practices.",
                            {.type = "semantic", .priority = 7.5,
                             .keywords = {"kernel", "security"}});
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r3->page_id, 3u);

    // Keyword recall — find pages mentioning "kernel"
    auto results = mem.recall({.task_description = "kernel task",
                                .top_k = 10});
    ASSERT_TRUE(results.has_value());
    EXPECT_GE(results->size(), 2u);  // pages 1 and 3 both have "kernel"

    // Recall by id
    auto by_id = mem.recall_by_id(2);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->page_id, 2u);

    // Recall recent
    auto recent = mem.recall_recent(2);
    ASSERT_TRUE(recent.has_value());
    EXPECT_EQ(recent->size(), 2u);
    EXPECT_EQ((*recent)[0].page_id, 3u);  // newest first

    // Forget + consolidate
    EXPECT_TRUE(mem.forget(2).has_value());
    EXPECT_TRUE(mem.consolidate().has_value());
    // After consolidation, page 2 should be gone
    auto mem_r2 = agent::Memory::open(tmp);
    ASSERT_TRUE(mem_r2.has_value());
    // Page 2 should be gone after consolidation
    auto by_id2 = mem_r2->recall_by_id(2);
    EXPECT_FALSE(by_id2.has_value());

    fs::remove(tmp);
}

TEST(AgentMemory, ReindexWithHashEmbedder) {
    namespace fs = std::filesystem;
    auto tmp = fs::temp_directory_path() / "v3_agent_mem_reindex.attf";
    fs::remove(tmp);

    auto mem_r = agent::Memory::open(tmp);
    ASSERT_TRUE(mem_r.has_value());
    auto& mem = *mem_r;
    EXPECT_TRUE(mem.remember("first memory", {}).has_value());
    EXPECT_TRUE(mem.remember("second memory", {}).has_value());

    embeddings::HashEmbedder emb(64);
    EXPECT_TRUE(mem.reindex(emb).has_value());

    // After reindex, embeddings should be present
    auto r = AttfReader::open_file(tmp);
    ASSERT_TRUE(r.has_value());
    auto idx = semantic::parse_index(*r);
    EXPECT_EQ(idx.entries.size(), 2u);

    fs::remove(tmp);
}
