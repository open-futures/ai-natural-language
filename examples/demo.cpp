// =============================================================================
//  examples/demo.cpp — End-to-end usage of the TTS SDK v3.0.
//
//  Build:
//    g++ -std=c++23 -Iinclude examples/demo.cpp \
//        -Lbuild/lib -ltts -lyaml-cpp -lzstd_static -o demo
//
//  Run:
//    ./demo
// =============================================================================
#include "tts/agent_memory.hpp"
#include "tts/attf_reader.hpp"
#include "tts/attf_writer.hpp"
#include "tts/compression.hpp"
#include "tts/embeddings.hpp"
#include "tts/encryption.hpp"
#include "tts/semantic_search.hpp"
#include "tts/sharded_reader.hpp"
#include "tts/streaming_writer.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/ttf_writer.hpp"
#include "tts/version.hpp"

#include <filesystem>
#include <iostream>

int main() {
    std::cout << "TTS SDK v" << tts::version() << "\n\n";

    // ----------------------------------------------------------------------
    //  TTF — write + read
    // ----------------------------------------------------------------------
    {
        tts::TtfWriter w;
        w.set_tfd_id(1).set_next_tfd(0);
        w.add_string_tag(256, "DocumentTitle", "TTS v3 Demo");
        w.add_section(1, "First section. UTF-8 friendly: 你好 🌍");
        w.add_section(2, "Second section with a different length.");
        auto st = w.write_to_file("demo.ttf");
        if (!st) { std::cerr << "write failed\n"; return 1; }
        std::cout << "Wrote demo.ttf ("
                  << std::filesystem::file_size("demo.ttf") << " bytes)\n";
    }
    {
        auto r = tts::TtfReader::open_file("demo.ttf");
        if (!r) { std::cerr << "open failed: " << tts::to_string(r.error()) << "\n"; return 1; }
        std::cout << "Read demo.ttf:\n";
        std::cout << "  tfd_id    : " << r->metadata().tfd_id << "\n";
        std::cout << "  sections  : " << r->metadata().sections.size() << "\n";
        auto s1 = r->get_section(1);
        if (s1) std::cout << "  section 1 : " << *s1 << "\n";
        auto v = r->validate_all();
        std::cout << "  validate  : " << (v ? "OK" : tts::to_string(v.error())) << "\n";
    }
    std::cout << "\n";

    // ----------------------------------------------------------------------
    //  ATTF — agent memory with embeddings
    // ----------------------------------------------------------------------
    {
        auto mem_r = tts::agent::Memory::open("demo.attf");
        if (!mem_r) { std::cerr << "memory open failed\n"; return 1; }
        auto& mem = *mem_r;

        tts::embeddings::HashEmbedder emb(64);

        auto r1 = mem.remember("You are a kernel optimization agent. Be terse.",
                                {.type="sys_prompt", .priority=9.9,
                                 .keywords={"kernel","agent","optimization"},
                                 .embedder=&emb});
        if (!r1) { std::cerr << "remember failed\n"; return 1; }

        auto r2 = mem.remember("User asked about memory allocators; suggested slab allocator.",
                                {.type="episodic", .priority=8.5,
                                 .keywords={"memory","alloc","slab"},
                                 .embedder=&emb});
        if (!r2) { std::cerr << "remember failed\n"; return 1; }

        auto r3 = mem.remember("Kernel security best practices.",
                                {.type="semantic", .priority=7.5,
                                 .keywords={"kernel","security"},
                                 .embedder=&emb});
        if (!r3) { std::cerr << "remember failed\n"; return 1; }

        std::cout << "Wrote demo.attf ("
                  << std::filesystem::file_size("demo.attf") << " bytes)\n\n";

        // Recall by keyword
        auto results = mem.recall({.task_description="kernel", .top_k=10});
        std::cout << "Keyword recall for 'kernel':\n";
        for (const auto& p : *results) {
            std::cout << "  page " << p.page_id << " [" << p.type << "]: "
                      << p.content.substr(0, 50) << "...\n";
        }

        // Hybrid recall (keyword + embedding)
        auto q = emb.embed("kernel task").value();
        auto hybrid = mem.recall({
            .task_description="kernel",
            .query_embedding=q,
            .top_k=3
        });
        std::cout << "\nHybrid recall (keyword + vector):\n";
        for (const auto& p : *hybrid) {
            std::cout << "  page " << p.page_id << " [" << p.type << "]\n";
        }
    }
    std::cout << "\n";

    // ----------------------------------------------------------------------
    //  Compression (M2)
    // ----------------------------------------------------------------------
    {
        std::string original(1000, 'x');
        auto compressed = tts::compression::compress_string(
            tts::compression::Algorithm::Zstd, original);
        if (compressed) {
            std::cout << "Compression: 1000 bytes -> " << compressed->size()
                      << " bytes (zstd)\n";
            auto decompressed = tts::compression::decompress_string(
                tts::compression::Algorithm::Zstd, *compressed);
            std::cout << "  round-trip OK: " << (decompressed && *decompressed == original)
                      << "\n";
        } else {
            std::cout << "Compression: zstd not available\n";
        }
    }
    std::cout << "\n";

    // ----------------------------------------------------------------------
    //  Encryption (M2)
    // ----------------------------------------------------------------------
    {
        tts::crypto::StaticKeyProvider key("default", "my_passphrase");
        auto k = key.get_key("default");
        std::string plaintext = "The quick brown fox jumps over the lazy dog.";
        tts::crypto::aes::Nonce nonce = tts::crypto::aes::random_nonce();
        auto ct = tts::crypto::aes::aes_gcm_encrypt(*k, nonce,
            std::span<const std::uint8_t>{
                reinterpret_cast<const std::uint8_t*>(plaintext.data()),
                plaintext.size()});
        auto pt = tts::crypto::aes::aes_gcm_decrypt(*k,
            std::span<const std::uint8_t>{ct.data(), ct.size()});
        std::cout << "Encryption (AES-256-GCM):\n";
        std::cout << "  plaintext  : " << plaintext << " (" << plaintext.size() << " bytes)\n";
        std::cout << "  ciphertext : " << ct.size() << " bytes (incl nonce + tag)\n";
        std::cout << "  round-trip : " << (!pt.empty() ? "OK" : "FAIL") << "\n";
    }

    // Clean up
    std::filesystem::remove("demo.ttf");
    std::filesystem::remove("demo.attf");
    return 0;
}
