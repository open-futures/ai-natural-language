# TTS SDK v3.5.2 — User Guide

This guide is for developers who want to **use** the TTS SDK to read or
write TTF/ATTF files, build agent memory systems, or compress/encrypt
content. For internals and contribution, see [DEV_GUIDE.md](DEV_GUIDE.md).

---

## 1. Installation

See [INSTALL.md](INSTALL.md) for full build instructions. Quick version:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Requires CMake ≥ 3.20 and a C++23 compiler (GCC 14+, Clang 17+, MSVC 19.36+).

---

## 2. Reading a TTF file

```cpp
#include "tts/ttf_reader.hpp"
#include <iostream>

void read_example(const std::filesystem::path& path) {
    auto r = tts::TtfReader::open_file(path);
    if (!r) {
        std::cerr << "open failed: " << tts::to_string(r.error()) << "\n";
        return;
    }

    // Parsed directory
    const auto& meta = r->metadata();
    std::cout << "TFD id: " << meta.tfd_id << "\n";
    std::cout << "Encoding: " << meta.encoding << "\n";
    std::cout << "Sections: " << meta.sections.size() << "\n";

    // Zero-copy fetch (hash-verified, constant-time comparison)
    auto content = r->get_section(1);
    if (!content) {
        std::cerr << "get_section failed: " << tts::to_string(content.error()) << "\n";
        return;
    }
    std::cout << "section 1 content: " << *content << "\n";

    // Bulk validate every section
    auto v = r->validate_all();
    if (!v) std::cerr << "validation failed: " << tts::to_string(v.error()) << "\n";

    // v3.0: follow next_tfd chain
    auto chain = r->follow_chain(path);
    if (chain) {
        std::cout << "Chain length: " << chain->size() << "\n";
        auto s2 = r->get_section_in_chain(2, *chain);
        if (s2) std::cout << "section 2 (via chain): " << *s2 << "\n";
    }
}
```

### Lifetime & threading

- `TtfReader` owns its `MemoryView`. All views returned by `get_section()`
  are valid only while the reader is alive.
- The reader is **thread-safe for concurrent reads**. The underlying mmap
  region is read-only.
- Writers are not thread-safe — build on a single thread.

---

## 3. Writing a TTF file

```cpp
#include "tts/ttf_writer.hpp"

void write_example() {
    tts::TtfWriter w;
    w.set_tfd_id(1).set_next_tfd(0);
    w.add_section(1, "First section content.");
    w.add_section(2, "Second section content.");
    w.write_to_file("output.ttf");
}
```

For very large documents (>100 MB), use the streaming writer:

```cpp
#include "tts/streaming_writer.hpp"

void stream_example() {
    tts::StreamingTtfWriter sw;
    sw.set_tfd_id(1);
    sw.add_section(1, "First section.");
    sw.add_section_file(2, "huge_section.txt");  // streamed from disk
    sw.write_to_file("huge.ttf");
}
```

---

## 4. Reading an ATTF file

```cpp
#include "tts/attf_reader.hpp"

void read_attf(const std::filesystem::path& path) {
    auto r = tts::AttfReader::open_file(path);
    if (!r) return;
    std::cout << "ATTF version: " << r->header().version << "\n";
    std::cout << "mem_zone: " << r->header().get_setting("mem_zone") << "\n";

    for (const auto& e : r->directory()) {
        std::cout << "page " << e.hex_address << " [" << e.type
                  << "] prio=" << e.priority << "\n";
    }

    auto page = r->get_page("0x00A1");        // zero-copy
    if (page) std::cout << "page content: " << *page << "\n";
}
```

### ATTF v3.0 escape sequences

Page content may now contain literal `\x03` (ETX) bytes — they are escaped
to `\x1b\x03` on write and un-escaped on read. This means ATTF pages can
now store arbitrary binary data without breaking the row delimiter.

The parser uses a zero-copy fast-path for pages with no escape bytes
(most content), falling back to a copy-based decoder when escapes are present.

---

## 5. Compression and encryption (M2)

```cpp
#include "tts/compression.hpp"
#include "tts/encryption.hpp"

void compress_example() {
    std::string original = "Some long text content...";
    auto compressed = tts::compression::compress_string(
        tts::compression::Algorithm::Zstd, original);
    if (compressed) {
        auto decompressed = tts::compression::decompress_string(
            tts::compression::Algorithm::Zstd, *compressed);
        // *decompressed == original
    }
}

void encrypt_example() {
    tts::crypto::StaticKeyProvider key("default", "my_passphrase");
    auto k = key.get_key("default");

    std::string plaintext = "secret content";
    tts::crypto::aes::Nonce nonce = tts::crypto::aes::random_nonce();
    auto ct = tts::crypto::aes::aes_gcm_encrypt(*k, nonce,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(plaintext.data()),
            plaintext.size()});
    auto pt = tts::crypto::aes::aes_gcm_decrypt(*k,
        std::span<const std::uint8_t>{ct.data(), ct.size()});
    // pt == plaintext bytes
}
```

Key providers:

- `StaticKeyProvider` — passphrase-derived via PBKDF2-HMAC-SHA256 (200k iter)
- `MultiKeyProvider` — composes multiple providers, tried in order
- `KmsKeyProvider` — abstract; AWS KMS / GCP KMS / Vault adapters (roadmap)

---

## 6. Atomic section replacement (M3)

```cpp
#include "tts/wal.hpp"

void replace_example(const std::filesystem::path& path) {
    auto r = tts::TtfReader::open_file(path);
    if (!r) return;
    // Replace section 1's content atomically:
    tts::atomic_replace_section(path, *r, 1, "new content");
    // The WAL sidecar at path + ".wal" logs the change before the swap.
    // On crash, the next open() replays the WAL.
}
```

---

## 7. Semantic search (M4)

```cpp
#include "tts/embeddings.hpp"
#include "tts/semantic_search.hpp"
#include "tts/attf_reader.hpp"

void search_example(const std::filesystem::path& path) {
    auto r = tts::AttfReader::open_file(path);
    if (!r) return;
    auto idx = tts::semantic::parse_index(*r);
    std::cout << "Index size: " << idx.entries.size() << " pages\n";

    // Build a query embedding
    tts::embeddings::HashEmbedder emb(idx.dim);
    auto q = emb.embed("kernel optimization");
    if (!q) return;

    // Flat search (brute-force, <10k pages)
    tts::semantic::SearchOpts opts;
    opts.top_k = 10;
    auto results = tts::semantic::search_flat(idx,
        std::span<const float>{q->data(), q->size()}, *r, opts);

    for (const auto& res : results) {
        std::cout << res.hex_address << " (score=" << res.score << "): "
                  << res.page_content << "\n";
    }

    // Hybrid search (keyword + vector)
    auto hybrid = tts::semantic::search_hybrid(*r, idx,
        std::span<const float>{q->data(), q->size()},
        "kernel optimization", 0.3f, 0.7f, opts);
}
```

Available embedders:

- `HashEmbedder` — deterministic, zero-dep, for testing
- `OpenAIEmbedder` — HTTP client for `text-embedding-3-small` (1536-dim)
- `LocalEmbedder` — ONNX Runtime + bge-small-en (roadmap)

---

## 8. Agent memory (M5)

The `tts::agent::Memory` class wraps the lower-level APIs into an
ergonomic, LLM-friendly surface:

```cpp
#include "tts/agent_memory.hpp"
#include "tts/embeddings.hpp"

void agent_example() {
    auto mem_r = tts::agent::Memory::open("memory.attf");
    if (!mem_r) return;
    auto& mem = *mem_r;

    tts::embeddings::HashEmbedder emb(64);

    // Append memories — embeddings auto-computed
    mem.remember("You are a kernel optimization agent.",
                 {.type="sys_prompt", .priority=9.9,
                  .keywords={"kernel","agent"}, .embedder=&emb});

    mem.remember("User asked about memory allocators.",
                 {.type="episodic", .priority=8.0,
                  .keywords={"memory","alloc"}, .embedder=&emb});

    // Recall
    auto recent = mem.recall_recent(5);
    for (const auto& p : *recent) {
        std::cout << "page " << p.page_id << ": " << p.content << "\n";
    }

    // Hybrid recall
    auto results = mem.recall({
        .task_description = "kernel task",
        .query_embedding = emb.embed("kernel").value(),
        .min_priority = 5.0,
        .top_k = 5
    });

    // Maintenance
    mem.forget(2);          // soft-delete (priority -> 0)
    mem.consolidate();      // drop forgotten pages
    mem.reindex(emb);       // re-embed all pages
}
```

---

## 9. Sharded readers (M7)

For corpora too large for a single file, use the `ShardedReader` to walk
the `next_tfd` chain:

```cpp
#include "tts/sharded_reader.hpp"

auto sr = tts::ShardedReader::open("shard1.ttf");
if (!sr) return;
std::cout << "Shards: " << sr->shard_count() << "\n";
auto s = sr->get_section(42);  // searches across all shards
```

---

## 10. CLI tools

```bash
ttsinspect inspect       <file>                # high-level summary
ttsinspect validate      <file>                # verify hashes/page lookups
ttsinspect query-section <file> --id <id>       # TTF: zero-copy section
ttsinspect query-page    <file> --addr <hex>   # ATTF: zero-copy page

ttslint <file>                                   # validate against v3.0 spec
```

---

## 11. Python binding (ctypes)

```python
import tts

with tts.TtfReader("doc.ttf") as r:
    print("Sections:", r.section_count())
    print("Section 1:", r.get_section(1))
    r.validate_all()

with tts.AttfReader("memory.attf") as r:
    page = r.get_page("0x00A1")
    print("Page:", page)
```

See `bindings/python/tts/__init__.py` for the full API.

---

## 12. Observability (M7)

Install a span sink to forward events to OpenTelemetry / Prometheus:

```cpp
#include "tts/observability.hpp"

tts::obs::set_span_sink([](const tts::obs::Span& s) {
    std::cerr << "[" << s.name << "] "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     s.end - s.start).count() << "us"
              << " ok=" << s.ok
              << " bytes=" << s.bytes_in << "/" << s.bytes_out << "\n";
});

// All subsequent reader/writer operations emit spans
{
    tts::obs::ScopedSpan span("my_app.read_section",
                              tts::obs::SpanKind::Read);
    auto r = tts::TtfReader::open_file("doc.ttf");
    auto s = r->get_section(1);
    span.set_bytes_in(1024).set_bytes_out(s->size()).set_ok(s.has_value());
}
```

---

## 13. FAQ

**Q: Are v2.0 files still readable?**
A: Yes. v3.0 readers gracefully ignore unknown fields. Files written
with v2.0 tools parse without modification.

**Q: How do I migrate a v2.0 file to v3.0 features (compression/encryption)?**
A: Open with a v3.0 reader, re-write with a v3.0 writer. There's no
automatic migration — v2.0 files remain valid forever.

**Q: Can I store binary data in ATTF pages?**
A: Yes, since v3.0. Any byte is allowed (including `\x03` — it gets
escaped to `\x1b\x03` on write).

**Q: What embedding model should I use?**
A: The SDK ships a `HashEmbedder` for testing only. For production, use
`OpenAIEmbedder` (HTTP client, requires `OPENAI_API_KEY`) or supply your
own `IEmbedder` implementation.

**Q: How do I rotate encryption keys?**
A: Re-write the file with a new key_id. Per-section encryption means you
can rotate one section at a time without re-encrypting the whole file.

---

## 14. v3.5 additions

### 14.1 Real OpenAI embeddings

```cpp
#include "tts/embeddings.hpp"

// With a real API key — uses POSIX sockets, no libcurl dependency.
tts::embeddings::OpenAIEmbedder emb("sk-...");

// Without a key — falls back to HashEmbedder so the search pipeline
// still works for local dev / testing.
tts::embeddings::OpenAIEmbedder fallback_emb("");

auto v = emb.embed("kernel optimization").value();
// v is a 1536-dim float vector (or 3072 for text-embedding-3-large).
```

To use a custom HTTP client (e.g. for HTTPS via libcurl in production),
subclass `IEmbedder` and pass your own implementation to
`Memory::remember()`.

### 14.2 AES-NI hardware detection

```cpp
#include "tts/encryption.hpp"

if (tts::crypto::aes::has_aesni_hardware_acceleration()) {
    std::cout << "AES-NI + PCLMULQDQ detected — hardware acceleration available\n";
} else {
    std::cout << "Using portable software AES\n";
}
```

The current build uses the portable C implementation for both paths
(AES-NI integration into the block cipher inner loop is on the v4.0
roadmap). The detection function lets you plan capacity and performance
budgets ahead of the v4.0 release.

### 14.3 KMS key providers

The KMS adapters (`AwsKmsKeyProvider`, `GcpKmsKeyProvider`,
`VaultKeyProvider`) live in `include/tts/kms_providers.hpp`. They
require the caller to supply an `IHttpTransport*` — the SDK does NOT
pull in the AWS SDK / GCP SDK / Vault client as hard dependencies.

```cpp
#include "tts/kms_providers.hpp"

// Caller-supplied transport — you implement the SigV4 signing here.
class MySigV4Transport : public tts::crypto::kms::IHttpTransport {
    tts::crypto::kms::HttpResponse
    request(const tts::crypto::kms::HttpRequest& req) override {
        // Sign req with AWS SigV4 using your credentials,
        // then send via your HTTP client of choice.
    }
};

auto transport = std::make_shared<MySigV4Transport>();
tts::crypto::kms::AwsKmsKeyProvider kms(
    "us-east-1", "AKID", "SECRET", transport);

auto key = kms.get_key("arn:aws:kms:us-east-1:123:key/abc");
// Use *key with aes_gcm_encrypt() / aes_gcm_decrypt().
```

Compose multiple providers via `MultiKeyProvider`:

```cpp
tts::crypto::MultiKeyProvider multi;
multi.add(aws_kms);
multi.add(vault);
multi.add(static_fallback);
```

### 14.4 `tts-tui` — interactive browser

```bash
# Either as a subcommand of ttsinspect:
./bin/ttsinspect tui memory.attf

# Or as a standalone binary:
./bin/tts-tui memory.attf
```

Keyboard:

- `↑` / `↓` — navigate sections / pages
- `q` or `Esc` — quit

The TUI auto-detects TTF vs ATTF and shows appropriate columns.

### 14.5 Node.js binding

See `bindings/node/` for `binding.gyp`, `index.cpp`, and `package.json`.
Build with `node-gyp`:

```bash
cd bindings/node
npm install
node-gyp configure && node-gyp build
node -e "const tts = require('./build/Release/tts'); console.log(tts.version());"
```

The binding exposes `version()`, `ttfOpen()`, `ttfGetSection()`,
`attfOpen()`, `attfGetPage()`, and `attfGetPageById()`.

### 14.6 PHILOSOPHY.md

The project's design constitution lives at the project root and in
`docs/`. Read it before proposing features — every accepted feature
must be defensible against the 12 principles.

