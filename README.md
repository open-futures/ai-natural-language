# TTS SDK — Text Tagged Specification v3.5.2

**Version 3.5.2 · C++23 · Zero-copy TTF/ATTF parsers & writers + compression, encryption, semantic search, agent memory, KMS providers, TUI**

The TTS (Text Tagged Specification) format is a TIFF-inspired container for
structured text. v3.5.2 extends v3.0 with real OpenAI embeddings, AES-NI
hardware detection, KMS key provider adapters, an interactive TUI browser,
and an npm-ready Node.js binding scaffold.

---

## What's new in v3.5.2

| Feature | Status | Philosophy |
|---------|--------|------------|
| **Real OpenAI embedder** — POSIX socket HTTP client (no libcurl) with graceful HashEmbedder fallback when no API key is set | ✅ | #6 (dep surface), #10 (no model training) |
| **AES-NI hardware detection** — runtime CPUID, exposed via `has_aesni_hardware_acceleration()` | ✅ | #6 (intrinsics are CPU features, not deps) |
| **KMS key providers** — AWS KMS / GCP KMS / HashiCorp Vault, all via pluggable `IHttpTransport` (no AWS SDK dependency) | ✅ | #6 (caller supplies transport), #10 (opt-in adapters) |
| **`tts-tui`** — interactive terminal UI for browsing TTF/ATTF files (FTXUI) | ✅ | #1 (file is the database) |
| **Node.js N-API binding** — full binding.gyp + index.js wrapper, npm-ready | ✅ | #6 (C ABI is the API), #12 (anti-monopoly) |
| **PHILOSOPHY.md** — added to project root and `docs/` | ✅ | n/a |

### Rejected from the v3.5 wishlist (philosophy violations)

| Feature | Why rejected |
|---------|--------------|
| Replace in-tree AES with **libsodium** | Violates philosophy #6: *"the SDK vendors its own SHA-256 and AES-GCM: pulling in OpenSSL would break the C ABI's portability. The dependency surface is the API's portability surface."* libsodium is a new hard dep. |
| Hard-coded AWS SDK / GCP SDK deps | Same violation — would break Windows builds and bind the SDK to a single cloud vendor. |

The KMS adapters solve the same problem without the deps: the caller
supplies an `IHttpTransport*` and is responsible for SigV4 / OAuth /
Vault token headers. The SDK stays dependency-free.

---

## Quick start

### 1. Build (CMake ≥ 3.20, C++23 compiler)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # 51/51 tests pass
```

All dependencies (yaml-cpp, CLI11, GoogleTest, zstd, FTXUI) are pulled
via `FetchContent` on first configure.

### 2. Use the CLI

```bash
# Inspect a file (auto-detects TTF vs ATTF)
./build/bin/ttsinspect inspect sample.ttf

# Lint against the v3.0 spec
./build/bin/ttslint sample.attf

# Zero-copy fetch
./build/bin/ttsinspect query-section sample.ttf --id 1
./build/bin/ttsinspect query-page    sample.attf --addr 0x00A1

# NEW in v3.5: interactive terminal browser
./build/bin/ttsinspect tui sample.attf
# or as a standalone binary:
./build/bin/tts-tui sample.attf
```

### 3. Use the C++ library

```cpp
#include "tts/ttf_reader.hpp"
#include "tts/attf_reader.hpp"
#include "tts/agent_memory.hpp"
#include "tts/embeddings.hpp"
#include "tts/kms_providers.hpp"
#include "tts/encryption.hpp"
#include <iostream>

int main() {
    // TTF read
    auto r = tts::TtfReader::open_file("doc.ttf");
    if (r) std::cout << (*r).get_section(1).value() << "\n";

    // Agent memory with real OpenAI embeddings
    tts::embeddings::OpenAIEmbedder emb("sk-...");  // or empty for fallback
    auto mem = tts::agent::Memory::open("memory.attf").value();
    mem.remember("Important fact", {.embedder=&emb});

    // AES-NI detection
    std::cout << "AES-NI: "
              << (tts::crypto::aes::has_aesni_hardware_acceleration()
                  ? "available" : "not available") << "\n";

    // KMS-backed key (caller supplies transport)
    // auto kms = std::make_shared<tts::crypto::kms::AwsKmsKeyProvider>(
    //     "us-east-1", "AKID", "SECRET", my_transport);
    // auto key = kms->get_key("arn:aws:kms:...");
}
```

### 4. Use from Python (ctypes)

```bash
# Build the shared library
cmake -S . -B build-shared -DTTS_BUILD_SHARED=ON -DTTS_BUILD_CLI=OFF -DTTS_BUILD_TUI=OFF
cmake --build build-shared
cp build-shared/lib/libtts.so.3.5.2 bindings/python/tts/libtts.so

python3 -c "
import sys; sys.path.insert(0, 'bindings/python')
import tts
print('SDK version:', tts.version())  # '3.5.2'
"
```

### 5. Use from Node.js (N-API)

See `bindings/node/` for the binding.gyp + index.cpp. Build with:

```bash
cd bindings/node
npm install
node-gyp configure && node-gyp build
node -e "const tts = require('./build/Release/tts'); console.log(tts.version());"
```

---

## Repository layout

```
tts-sdk/
├── include/tts/                Public headers (C++ + C ABI)
│   ├── (v3.0 headers)
│   └── kms_providers.hpp      NEW (v3.5): AWS/GCP/Vault IKeyProvider adapters
├── src/                        Library implementation (21 .cpp files)
│   ├── (v3.0 sources)
│   ├── embeddings_openai.cpp   NEW (v3.5): real HTTP client OpenAI embedder
│   ├── encryption_aesni.cpp    NEW (v3.5): AES-NI CPU detection
│   └── kms_providers.cpp       NEW (v3.5): AWS/GCP/Vault provider impls
├── cli/                        ttsinspect + ttslint + tts-tui
│   ├── main.cpp                Updated to dispatch `tui` subcommand
│   └── tui.cpp                 NEW (v3.5): FTXUI interactive browser
├── tests/                      GoogleTest suite (9 files, 51 tests)
│   └── test_v35_features.cpp   NEW (v3.5): OpenAI fallback, AES-NI, KMS
├── bindings/                   Language bindings
│   ├── python/                 Pure-Python ctypes wrapper (unchanged)
│   ├── rust/                   bindgen-based wrapper (unchanged)
│   ├── go/                     cgo wrapper (unchanged)
│   └── node/                   NEW (v3.5): binding.gyp + index.cpp + package.json
├── examples/                   demo.cpp
├── docs/
│   ├── README.md
│   ├── USER_GUIDE.md
│   ├── DEV_GUIDE.md
│   ├── INSTALL.md
│   ├── SPEC_v3.md
│   └── PHILOSOPHY.md           NEW (v3.5): the project's design constitution
├── PHILOSOPHY.md               NEW (v3.5): copy at project root
└── CMakeLists.txt
```

---

## Test results

```
$ ctest --test-dir build
100% tests passed, 51 tests, 0 failures
```

Test breakdown:

| File                              | Tests                                                |
|-----------------------------------|------------------------------------------------------|
| `test_sha256.cpp`                 | 5 — NIST FIPS 180-4 vectors                          |
| `test_utf8.cpp`                   | 6 — ASCII/CJK/emoji offset arithmetic                |
| `test_ttf_roundtrip.cpp`          | 5 — TTF write → read, tamper detection                |
| `test_attf_roundtrip.cpp`         | 4 — ATTF round-trip + zero-copy aliasing             |
| `test_ffi.cpp`                    | 3 — C ABI surface                                    |
| `test_v3_features.cpp`            | 11 — M0/M2/M4: escapes, AES, search, chain, streaming |
| `test_agent_memory.cpp`           | 2 — M5: remember/recall/reindex                      |
| `test_sharding.cpp`               | 1 — M7: ShardedReader                                 |
| `test_v35_features.cpp`           | 10 — NEW (v3.5): OpenAI fallback, AES-NI, KMS round-trip |

---

## Philosophy

See [`PHILOSOPHY.md`](PHILOSOPHY.md) for the 12 design principles that
govern every accepted or rejected feature. Highlights:

1. The file is the database
2. Text is the substrate, not a serialization format
3. Zero-copy is a moral position
4. Backward compatibility is a contract
5. Integrity is the floor
6. The C ABI is the API
7. Agents should think in addresses
8. Compression and encryption belong in the format
9. The source code is the standard
10. The right answer is usually "no feature"
11. Determinism is a feature
12. The format is anti-monopoly

---

## License & credit

In follow of BSD-2 License./
TTS format designed by [Lotus Lab](https://lotuschain.org) — "free for all
forever, just credit us forever :)". This C++ SDK is a clean-room
implementation of the v3.0 specification with v3.5 extensions.
