# TTS SDK v3.5.2 — Developer Guide

For contributors and SDK maintainers. Documents the internal architecture,
v3.0 design decisions, and how to extend the SDK.

---

## 1. Architecture overview (v3.0)

```
┌──────────────────────────────────────────────────────────────────┐
│                          Public headers                           │
│  include/tts/                                                     │
│    types.hpp          Error enums + std::expected aliases         │
│    memory_view.hpp    Borrowed / owned / mmap byte source          │
│    utf8_utils.hpp     UTF-8 char arithmetic                        │
│    sha256.hpp         FIPS 180-4 + constant_time_equals (M0)       │
│    compression.hpp    zstd interface (M2)                           │
│    encryption.hpp     AES-256-GCM + IKeyProvider (M2)             │
│    ttf_reader.hpp     TtfReader + chain traversal (M0)             │
│    ttf_writer.hpp     TtfWriter                                     │
│    streaming_writer.hpp  Chunked streaming writer (M0)              │
│    attf_reader.hpp    AttfReader with escape support (M0)          │
│    attf_writer.hpp    AttfWriter with escape encoding (M0)        │
│    wal.hpp            Write-ahead log + atomic replace (M3)         │
│    embeddings.hpp     IEmbedder + Hash/OpenAI refs (M4)            │
│    semantic_search.hpp  Tag 402 search: flat/HNSW/hybrid (M4)      │
│    agent_memory.hpp   High-level Memory class (M5)                 │
│    sharded_reader.hpp  Multi-file chain reader (M7)                │
│    observability.hpp  OpenTelemetry span hooks (M7)                │
│    ffi.h              C ABI                                        │
│    version.hpp        Version constants                            │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                          Library (libtts)                         │
│  src/  — 17 translation units                                     │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                           Consumers                                │
│  cli/ttsinspect, cli/ttslint                                       │
│  tests/  (8 test files, 41 tests)                                  │
│  examples/demo.cpp                                                 │
│  bindings/python | rust | go | node                               │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. v3.0 milestone summary

| M | Goal | Key files |
|---|------|-----------|
| 0 | Foundation hardening | `sha256.hpp` (CT compare), `ttf_reader.cpp` (chain), `streaming_writer.{hpp,cpp}`, `attf_reader.cpp` (escapes) |
| 1 | Language bindings | `bindings/python/`, `bindings/rust/`, `bindings/go/`, `bindings/node/` |
| 2 | Compression + encryption | `compression.{hpp,cpp}`, `encryption.{hpp,cpp}` |
| 3 | ACID / WAL | `wal.{hpp,cpp}` |
| 4 | Semantic search | `embeddings.{hpp,cpp}`, `semantic_search.{hpp,cpp}` |
| 5 | Agent memory runtime | `agent_memory.{hpp,cpp}` |
| 6 | Spec v3.0 + lint | `docs/SPEC_v3.md`, `cli/lint.cpp` |
| 7 | Sharding + observability | `sharded_reader.{hpp,cpp}`, `observability.{hpp,cpp}` |

---

## 3. Core abstractions

### 3.1 `MemoryView` (unchanged from v2.0)

Three backends: Borrowed, Owned, Mmap. See v2.0 DEV_GUIDE.

### 3.2 `crypto::Sha256` (M0 addition)

v3.0 adds constant-time comparison helpers to prevent timing oracles on
hash verification:

```cpp
[[nodiscard]] bool constant_time_equals_hex(
    std::string_view a, std::string_view b) noexcept;
```

The TTF reader uses this for hash verification. Both inputs are
case-normalised first; comparison walks the longer of the two buffers
regardless of where they diverge.

### 3.3 `compression::Algorithm` (M2)

```cpp
enum class Algorithm : std::uint8_t { None, Zstd };
TtfResult<std::vector<std::uint8_t>>
compress(Algorithm algo, std::span<const std::uint8_t> input, int level = 3);
```

The `TTS_USE_ZSTD` CMake option controls availability. When off, requests
for `Zstd` return `TtfError::InvalidArgument`.

### 3.4 `crypto::aes` (M2)

In-tree AES-256-GCM implementation (no OpenSSL / Crypto++ dependency).
Produces `nonce(12) || ciphertext || tag(16)` for storage in the TTF
data stream. The implementation is constant-time-ish on the AES core
but **NOT hardened** against cache-based side channels — wrap with
AES-NI or libsodium for production.

```cpp
namespace aes {
    using Key   = std::array<std::uint8_t, 32>;
    using Nonce = std::array<std::uint8_t, 12>;
    using Tag   = std::array<std::uint8_t, 16>;

    Key derive_from_passphrase(std::string_view, std::span<const uint8_t> salt,
                              uint32_t iterations = 200'000);
    Nonce random_nonce();
    std::vector<std::uint8_t> aes_gcm_encrypt(...);
    std::vector<std::uint8_t> aes_gcm_decrypt(...);  // empty on auth failure
}
```

### 3.5 `IKeyProvider` (M2)

```cpp
class IKeyProvider {
    virtual std::shared_ptr<const aes::Key> get_key(std::string_view key_id) const = 0;
    virtual std::vector<std::string> key_ids() const = 0;
};
```

Built-in implementations:
- `StaticKeyProvider` — single passphrase-derived key
- `MultiKeyProvider` — composes multiple providers, tried in order

For KMS-backed providers, subclass `IKeyProvider` and implement `get_key`
to make the appropriate RPC.

### 3.6 `TtfWal` (M3)

Append-only sidecar file at `<base>.ttf.wal`:

```
[WALv1]\n
PUT <section_id> <offset_chars> <length_chars> <sha256_hex>\n
DEL <section_id>\n
COMMIT <tx_id>\n
```

- Writers append ops + commit before swapping the new TTF in
- Readers replay the WAL up to the last COMMIT line they observe
- After a successful atomic swap, the WAL is truncated

`atomic_replace_section()` is the convenience helper for the common case
of updating a single section.

### 3.7 `IEmbedder` (M4)

```cpp
class IEmbedder {
    virtual TtfResult<std::vector<float>> embed(
        std::string_view text, const EmbeddingOptions& opts = {}) const = 0;
    virtual std::string model_name() const = 0;
    virtual std::uint32_t dim() const = 0;
};
```

Reference implementations:
- `HashEmbedder` — deterministic, zero-dep, for tests
- `OpenAIEmbedder` — HTTP client stub for `text-embedding-3-small`
- `LocalEmbedder` — ONNX Runtime + bge-small-en (roadmap)

### 3.8 `semantic::ParsedIndex` (M4)

v3.0 stores embeddings inline in page content with a `\x00EMB:` prefix
(this avoids a separate Tag 402 row format while keeping the embedding
metadata together with the page). `parse_index(reader)` walks the
directory, extracts embeddings, and parses the model/dim/metric
settings from the header.

```cpp
struct ParsedIndex {
    std::vector<EmbeddingEntry> entries;
    std::string model;
    std::uint32_t dim;
    Metric metric = Metric::Cosine;
};
```

Three search functions, all returning `std::vector<SearchResult>`:

- `search_flat` — brute-force linear scan, for <10k pages
- `search_hnsw` — small inline HNSW-lite, for 10k–1M pages
- `search_hybrid` — combines Tag 401 keyword matches with Tag 402 vectors

### 3.9 `agent::Memory` (M5)

The high-level API:

```cpp
class Memory {
    static TtfResult<Memory> open(std::filesystem::path);
    TtfResult<PageRef> remember(std::string_view content, const RememberOpts&);
    TtfResult<std::vector<PageRef>> recall(const RecallQuery&) const;
    TtfResult<PageRef> recall_by_id(std::uint32_t) const;
    TtfResult<PageRef> recall_by_address(std::string_view) const;
    TtfResult<std::vector<PageRef>> recall_recent(std::uint32_t n = 10) const;
    TtfStatus reindex(embeddings::IEmbedder&);
    TtfStatus forget(std::uint32_t page_id);
    TtfStatus consolidate();
};
```

`remember()` is **not** incremental — it rewrites the entire file with
the new page appended. This is the simplest correct model; an
incremental append API is on the roadmap.

### 3.10 `ShardedReader` (M7)

Walks the `next_tfd` chain across multiple files:

```cpp
class ShardedReader {
    static TtfResult<ShardedReader> open(const std::filesystem::path& root,
                                         std::string_view chain_suffix = ".next.ttf",
                                         std::size_t max_shards = 64);
    TtfResult<std::string_view> get_section(std::uint32_t) const noexcept;
    TtfResult<std::pair<std::size_t, std::string_view>>
        get_section_with_shard(std::uint32_t) const noexcept;
    std::size_t shard_count() const noexcept;
};
```

### 3.11 `obs::ScopedSpan` (M7)

RAII span emitter. Calls the global `SpanSink` on destruction. By
default the sink is null (no overhead). Install one to forward events
to OpenTelemetry, Prometheus, etc.

```cpp
{
    obs::ScopedSpan span("read_section", obs::SpanKind::Read);
    auto s = reader.get_section(id);
    span.set_bytes_in(1024).set_bytes_out(s ? s->size() : 0).set_ok(s.has_value());
}
```

---

## 4. C ABI design (v3.0 additions)

New FFI functions in v3.0:

```c
// Semantic search iterator (M4)
int tts_attf_search_open(tts_attf_reader_t, const float* query, uint32_t dim,
                          uint32_t top_k, tts_attf_search_iter_t* out);
int tts_attf_search_next(tts_attf_search_iter_t, char* buf, size_t cap, size_t* len, float* score);
int tts_attf_search_close(tts_attf_search_iter_t);

// Feature flags (M2)
int tts_has_zstd(void);
int tts_has_aes_gcm(void);
```

The semantic search iterator uses a three-call pattern (open / next /
close) instead of returning all results at once, to avoid leaking C++
containers across the language boundary.

---

## 5. Testing

### Test layout

| File                          | Covers                                   |
|-------------------------------|------------------------------------------|
| `test_sha256.cpp`             | FIPS 180-4 vectors, streaming            |
| `test_utf8.cpp`              | ASCII/CJK/emoji, offset conversion       |
| `test_ttf_roundtrip.cpp`     | TTF round-trip, tamper detection          |
| `test_attf_roundtrip.cpp`    | ATTF round-trip, zero-copy aliasing       |
| `test_ffi.cpp`               | C ABI surface                            |
| `test_v3_features.cpp`        | M0/M2/M4 — escapes, CT, compression, AES, search, chain, streaming |
| `test_agent_memory.cpp`       | M5 — remember/recall/forget/reindex       |
| `test_sharding.cpp`           | M7 — ShardedReader walks chain            |

Run all tests:

```bash
ctest --test-dir build --output-on-failure
```

Current status: **41/41 passing**.

---

## 6. Extension points

### Adding a new embedding model

Subclass `IEmbedder`:

```cpp
class MyEmbedder : public tts::embeddings::IEmbedder {
    TtfResult<std::vector<float>> embed(std::string_view text,
                                         const EmbeddingOptions& opts) const override {
        // Call your model, return floats
    }
    std::string model_name() const override { return "my-model-v1"; }
    std::uint32_t dim() const override { return 768; }
};
```

Then pass it to `Memory::remember(opts.embedder=...)` or
`Memory::reindex(emb)`.

### Adding a new key provider

Subclass `IKeyProvider`:

```cpp
class AwsKmsKeyProvider : public tts::crypto::IKeyProvider {
    std::shared_ptr<const aes::Key> get_key(std::string_view key_id) const override {
        // Call KMS, return the derived key
    }
    std::vector<std::string> key_ids() const override { /* ... */ }
};
```

Compose with `MultiKeyProvider` for fallback chains.

### Adding a new index type

Implement a new `search_<type>` function with the same signature:

```cpp
std::vector<SearchResult> search_mytype(
    const ParsedIndex& idx,
    std::span<const float> query,
    const AttfReader& reader,
    const SearchOpts& opts);
```

---

## 7. Known limitations (v3.0)

- **AES is not constant-time.** Cache-based side channels are possible.
  Use libsodium or AES-NI for production crypto.
- **No streaming writer for ATTF.** Only TTF has `StreamingTtfWriter`.
- **Hash embedder is for tests only.** No semantic value — use a real
  embedding model for production search.
- **`Memory::remember()` is not incremental.** Rewrites the whole file.
- **ShardedReader chain depth is bounded by `max_shards=64`**.
- **HNSW-lite is single-level** (no hierarchy). Adequate for 10k–1M pages
  but slower than full hnswlib for >1M.

---

## 8. v3.0 roadmap (post-release)

These items are tracked but not implemented in v3.0:

- Constant-time AES (AES-NI intrinsics)
- Streaming ATTF writer
- LocalEmbedder via ONNX Runtime
- KMS key provider adapters (AWS / GCP / Vault)
- Multi-writer concurrency (currently single-writer MVCC-lite)
- PQ + IVF index for >1M pages
- `ttsinspect tui` — terminal UI browser
- VS Code extension
- Conformance test suite (separate repo, language-agnostic)
- Lotus Lab tag registry
- `ttslint --fix` for auto-remediation

---

## 9. v3.5 architecture additions

### 9.1 Real OpenAI embedder (`src/embeddings_openai.cpp`)

Uses POSIX sockets (`<sys/socket.h>`, `<netdb.h>`) — no libcurl.
For HTTPS in production, either front with stunnel or replace via
your own `IEmbedder` that uses your HTTP client. When no API key is
set, falls back to `HashEmbedder` so the search pipeline works in
offline / test environments.

This design follows philosophy #6 ("dependency surface is the API's
portability surface") and #10 ("no proprietary embedding model").

### 9.2 AES-NI detection (`src/encryption_aesni.cpp`)

Runtime CPUID detection of AES-NI + PCLMULQDQ. The current build
uses the portable C path for both detected and undetected CPUs
(AES-NI integration into the block cipher is on the v4.0 roadmap).
The function `has_aesni_hardware_acceleration()` is exposed publicly
so callers can plan capacity.

The v3.5 wishlist originally proposed replacing in-tree AES with
libsodium. **This was rejected** because it would add a hard
dependency, violating philosophy #6. AES-NI intrinsics are CPU
features, not dependencies — they don't violate the philosophy.

### 9.3 KMS key providers (`src/kms_providers.cpp`, `include/tts/kms_providers.hpp`)

Three adapter classes:
- `AwsKmsKeyProvider` — calls KMS Decrypt
- `GcpKmsKeyProvider` — calls Cloud KMS decrypt
- `VaultKeyProvider` — calls Vault's KV v2 read

All three take a caller-supplied `IHttpTransport*`. The SDK never
depends on AWS SDK / GCP SDK / Vault client. The caller is
responsible for SigV4 / OAuth / Vault token headers — typically by
wrapping their existing HTTP client in an `IHttpTransport`
implementation.

This pattern follows philosophy #6: the SDK stays dependency-free,
production users get KMS support, and the adapter is opt-in (the
header is always available, but production callers must supply the
transport).

### 9.4 TUI (`cli/tui.cpp`)

FTXUI-based interactive browser. Compiled into `ttsinspect` (so the
`tui` subcommand works in the main binary) and also built as a
standalone `tts-tui` binary (controlled by `TTS_TUI_STANDALONE`
macro). The standalone binary uses CLI11 for arg parsing.

The TUI is intentionally minimal — it's a browser, not an editor.
Editing TTF files via TUI would violate philosophy #2 ("text is the
substrate") by reintroducing an editor layer. Users who want to edit
should open the file in `vim`.

### 9.5 Node.js binding (`bindings/node/`)

`binding.gyp` + `index.cpp` (N-API) + `package.json` + `index.js`
wrapper. The binding covers the core FFI surface:
`version()`, `ttfOpen()`, `ttfGetSection()`, `attfOpen()`,
`attfGetPage()`, `attfGetPageById()`.

Build with `node-gyp configure && node-gyp build`. The resulting
`build/Release/tts.node` can be `require()`'d from Node.js.

### 9.6 PHILOSOPHY.md

The project's design constitution. Append-only — new principles can
be added, but existing ones cannot be removed or weakened. When
evaluating a feature request, refer to this file first.

