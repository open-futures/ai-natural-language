# Text Tagged Specification (TTS) — Version 3.5

**Status:** Stable reference implementation
**Maintained by:** Lotus Lab — https://lotuschain.org
**License:** Free for all forever, just credit us forever :)

---

## 1. Background

TTS v2.0 introduced two formats:

- **TTF** — Human/software-oriented: YAML directory + plain-text data stream
- **ATTF** — Agent/LLM-oriented: single-byte control characters + page addressing

TTS v3.0 extends both formats with optional features for production use:

- Per-section **compression** (zstd)
- Per-section **encryption** (AES-256-GCM)
- ATTF **escape sequences** for arbitrary binary content
- **Multi-document chaining** via `next_tfd`
- **Semantic embeddings** stored alongside content (Tag 402)
- **Hybrid keyword + vector search** at the format level

All v3.0 features are **backward-compatible**: a v2.0 reader will successfully
parse a v3.0 file (ignoring unknown fields), and a v3.0 reader will successfully
parse a v2.0 file (treating absent fields as defaults).

---

## 2. TTF v3.0 — File Format

### 2.1 Overall structure (unchanged from v2.0)

```
[TTF_LE:2.0]              ← magic header (note: version string stays "2.0"
---                       ← TFD open
tfd:                      ← Text File Directory (YAML)
  ...
---
[DATA_STREAM_START]
<data stream>
[TTF_EOF]
```

The magic header `[TTF_LE:2.0]` is preserved for backward compatibility.
The version is bumped to `3.0` in the YAML metadata, not in the magic.

### 2.2 New SectionLayout fields

```yaml
- section: 1
  offset_chars: 0
  length_chars: 1234
  sha256: "..."
  compression: "zstd"        # NEW (v3.0). Default: "none".
  encryption: "aes-256-gcm"  # NEW (v3.0). Default: "none".
  key_id:     "default"      # NEW (v3.0). Required when encryption != "none".
```

**Semantics:**

- `sha256` is always computed over the **plaintext, uncompressed** bytes.
- If `compression` != "none", the data stream stores the *compressed* bytes.
- If `encryption` != "none", the data stream stores `nonce(12) || ciphertext || tag(16)`.
- The pipeline is: plaintext → compress → encrypt → store. SHA-256 is on plaintext.

### 2.3 Multi-document chaining

The `next_tfd` field in the `tfd:` block has been part of TTF since v2.0 but
was previously undocumented. In v3.0 it is formalized:

- `next_tfd: 0` — no chain (default).
- `next_tfd: <id>` — a chained TTF file exists at `<this_path>.next.ttf`.
  The chained file's `tfd_id` should match this `next_tfd` value.

Readers SHOULD follow the chain transparently for `get_section_in_chain()`.
Writers SHOULD NOT chain automatically; chaining is an explicit user choice.

### 2.4 Tag namespaces (reserved)

| Range | Purpose                                   |
|-------|-------------------------------------------|
| 1–99  | TTF metadata (DocumentTitle, Author, …)   |
| 100–199 | Document structure (TOC, Index, …)      |
| 200–255 | Reserved for future TTF core use        |
| 256–272 | Free-form string tags (user-defined)    |
| 273   | SectionLayouts (TTF core)                 |
| 274–299 | Reserved for future TTF core use         |
| 300–399 | Application-specific tags (user-defined) |
| 400–499 | ATTF-specific tags (see §3)              |
| 500–599 | ML / embedding metadata                  |
| 600–699 | Provenance / audit trail                 |
| 700–799 | Governance / policy / DRM                |
| 800–899 | Reserved for Lotus Lab extensions        |
| 900+  | Unreserved                                 |

---

## 3. ATTF v3.0 — File Format

### 3.1 Control characters (unchanged from v2.0)

| Symbol | Hex   | Meaning                       |
|--------|-------|-------------------------------|
| ␁      | 0x01  | Start of heading              |
| ␄      | 0x04  | Global settings               |
| ␃      | 0x03  | End of entry / row delimiter  |
| ␂      | 0x02  | Start of data stream          |

### 3.2 NEW: Escape sequences (v3.0)

To allow storing arbitrary binary content (including literal `0x03` bytes)
in page bodies, v3.0 introduces the escape introducer `0x1b` (ESC):

| Sequence | Meaning                |
|----------|------------------------|
| `0x1b 0x03` | Literal ETX byte     |
| `0x1b 0x1b` | Literal ESC byte     |
| `0x1b <other>` | Reserved for future use; parsers should pass both bytes verbatim |

Writers MUST escape every `0x03` and `0x1b` in page content. Readers MUST
un-escape on read.

### 3.3 NEW: Tag 402 — Embedding index

```
␃402:<hex_address>,<float>,<float>,...,<float>\n
```

Each row stores one page's embedding as a comma-separated float list.

The embedding model and dimensionality are declared in the header:

```
␄mem_zone:active|last_sync:...|embedding_model:text-embedding-3-small|embedding_dim:1536|metric:cosine
```

### 3.4 NEW: Header version bump

```
␁ATTF_MEM:3.0
```

Version `2.0` files remain valid for v3.0 readers. Writers producing files
with v3.0 features (escapes, Tag 402) SHOULD emit `3.0`.

---

## 4. Search semantics

### 4.1 Keyword search (Tag 401)

For each directory entry, count the number of Tag 401 keywords that are
substrings of the query string. Higher count = higher score. Default weight:
1.0 per match.

### 4.2 Semantic search (Tag 402)

For each directory entry with an embedding, compute the similarity between
the query embedding and the stored embedding under the file's declared
metric (cosine / euclidean / dotproduct). Higher = better.

### 4.3 Hybrid search

```
final_score = keyword_weight * keyword_score + semantic_weight * semantic_score
```

Default weights: `keyword_weight = 0.3`, `semantic_weight = 0.7`.

---

## 5. Conformance

A conforming TTS v3.0 implementation MUST:

- Parse any v2.0 or v3.0 TTF file without error (ignoring unknown fields).
- Parse any v2.0 or v3.0 ATTF file without error (handling escapes).
- Verify SHA-256 hashes in constant time.
- Support `next_tfd` chain traversal.
- Support `compression: "none"` and at least one of (`"zstd"`).
- Support `encryption: "none"` and `aes-256-gcm`.
- Expose a C ABI compatible with `include/tts/ffi.h`.

A conforming implementation MAY:

- Implement HNSW or PQ+IVF index types for Tag 402 (Flat is the minimum).
- Implement streaming writers for very large files.
- Implement KMS-backed key providers.
- Add new tag numbers in the reserved ranges (900+).

---

## 6. Migration from v2.0

If you have existing v2.0 files:

1. Open them with a v3.0 reader — they parse as-is.
2. To add embeddings, call `Memory::reindex(embedder)` — this rewrites the
   file as v3.0 with Tag 402 entries.
3. To add compression/encryption, manually migrate via the writer:

```cpp
auto reader = TtfReader::open_file("old.ttf");
TtfWriter w;
// ... copy sections, set compression/encryption on writer
w.write_to_file("new.ttf");
```

There is no automatic migration — v2.0 files remain valid forever.

---

## 7. Changelog

### v3.5 (this release)
- **Real OpenAI embedder**: POSIX socket HTTP client (no libcurl), with
  graceful HashEmbedder fallback when no API key is set
- **AES-NI hardware detection**: runtime CPUID, exposed via
  `has_aesni_hardware_acceleration()`
- **KMS key providers**: AWS KMS / GCP KMS / HashiCorp Vault adapters
  via pluggable `IHttpTransport` (no AWS SDK / GCP SDK / Vault client
  hard deps — opt-in adapters gated by CMake options)
- **`tts-tui`**: interactive terminal UI for browsing TTF/ATTF files
  (FTXUI-based, available as `ttsinspect tui` or standalone `tts-tui`)
- **Node.js N-API binding**: full binding.gyp + index.cpp wrapper,
  npm-ready
- **PHILOSOPHY.md**: project's design constitution added at root and
  in `docs/`
- Rejected: replacing in-tree AES with libsodium (violates philosophy
  #6 — dependency surface is portability surface). AES-NI intrinsics
  chosen instead because CPU features are not "dependencies."
- Test count: 41 → 51 (+10 v3.5 tests covering OpenAI fallback,
  AES-NI detection, KMS adapter round-trip)

### v3.0
- Per-section compression (zstd)
- Per-section encryption (AES-256-GCM with key provider interface)
- ATTF escape sequences (binary-safe content)
- `next_tfd` chain traversal formalised
- Tag 402: embedding index
- Hybrid keyword + vector search
- Constant-time SHA-256 comparison
- Streaming writer API
- C ABI additions for v3 features

### v2.0
- Initial public release.
- TTF and ATTF formats.
- SHA-256 integrity.
- Single-byte control characters for ATTF.
