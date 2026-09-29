# TTS SDK v3.5.2 — Installation

## 1. Pre-built binaries (Linux x86_64)

This release ships with pre-built binaries in `bin/` and `lib/`:

| Path                | Description                                |
|---------------------|--------------------------------------------|
| `bin/ttsinspect`    | CLI: inspect/validate/query-section/query-page |
| `bin/ttslint`       | CLI: validate against v3.0 spec             |
| `lib/libtts.a`      | Static library                             |
| `lib/libtts.so.3`   | Shared library (for Python binding)        |

Quick smoke test:

```bash
cd tts-sdk-3.0.0
./bin/ttsinspect inspect samples/sample3.ttf
./bin/ttsinspect inspect samples/sample3.attf
./bin/ttslint samples/sample3.ttf
```

---

## 2. Build from source

### Prerequisites

- CMake ≥ 3.20
- C++23 compiler: GCC ≥ 14, Clang ≥ 17, MSVC ≥ 19.36
- C compiler (for zstd)
- Git (for `FetchContent`)

### Configure + build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build                # 41/41 tests pass
```

First-time configure fetches (via `FetchContent`):

- `yaml-cpp` 0.8.0 — TTF directory parsing
- `CLI11` v2.4.2 — CLI tools
- `GoogleTest` v1.14.0 — test suite
- `zstd` v1.5.6 — compression (M2)

### CMake options

| Option              | Default | Effect                                    |
|---------------------|---------|-------------------------------------------|
| `TTS_BUILD_TESTS`   | `ON`    | Build the GoogleTest suite                 |
| `TTS_BUILD_CLI`     | `ON`    | Build `ttsinspect` + `ttslint`            |
| `TTS_BUILD_TUI`     | `ON`    | Build `tts-tui` (requires FTXUI, fetched) |
| `TTS_BUILD_SHARED`  | `OFF`   | Build `libtts` as a shared library        |
| `TTS_USE_MMAP`      | `ON`    | Enable `mmap`/`CreateFileMapping` backend |
| `TTS_USE_ZSTD`      | `ON`    | Build with zstd compression support       |
| `TTS_USE_AWS_KMS`   | `OFF`   | Compile AWS KMS key provider adapter       |
| `TTS_USE_GCP_KMS`   | `OFF`   | Compile GCP KMS key provider adapter     |
| `TTS_USE_VAULT`     | `OFF`   | Compile HashiCorp Vault key provider       |
| `TTS_WERROR`        | `OFF`   | Treat warnings as errors                  |

Example: build a shared lib without tests, without zstd:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
                -DTTS_BUILD_SHARED=ON \
                -DTTS_BUILD_TESTS=OFF \
                -DTTS_USE_ZSTD=OFF
cmake --build build -j
```

---

## 3. Build outputs

After `cmake --build`, you'll find:

```
build/
├── bin/
│   ├── ttsinspect           # CLI tool
│   ├── ttslint              # Linter
│   └── tts_tests            # Test executable (if TTS_BUILD_TESTS=ON)
└── lib/
    ├── libtts.a             # Static library (default)
    ├── libtts.so.3.0.0     # Shared library (if TTS_BUILD_SHARED=ON)
    ├── libtts.so.3         # Symlink to .so.3.0.0
    ├── libtts.so           # Symlink to .so.3
    ├── libyaml-cpp.a       # Vendored yaml-cpp
    └── libzstd_static.a    # Vendored zstd (if TTS_USE_ZSTD=ON)
```

---

## 4. Linking from your own CMake project

### Option A: `add_subdirectory`

```cmake
add_subdirectory(tts-sdk)
target_link_libraries(your_app PRIVATE tts)
```

`yaml-cpp` and `zstd_static` are transitively available.

### Option B: install + find_package (future)

```bash
cmake --install build --prefix /usr/local
```

```cmake
find_package(tts 3.0 REQUIRED)
target_link_libraries(your_app PRIVATE tts::tts)
```

### Manual linking (no CMake)

```bash
g++ -std=c++23 -Iinclude my_app.cpp \
    -Llib -ltts -lyaml-cpp -lzstd_static \
    -lpthread \
    -o my_app
```

---

## 5. Language bindings

### Python (ctypes)

```bash
# 1. Build the shared library
cmake -S . -B build-shared -DTTS_BUILD_SHARED=ON -DTTS_BUILD_CLI=OFF -DTTS_BUILD_TESTS=OFF
cmake --build build-shared

# 2. Copy the shared library next to the Python module
cp build-shared/lib/libtts.so.3.0.0 bindings/python/tts/libtts.so

# 3. Use it
python3 -c "
import sys; sys.path.insert(0, 'bindings/python')
import tts
print('Version:', tts.version())
"
```

### Rust (bindgen)

The scaffold in `bindings/rust/` uses raw extern declarations (no
bindgen dependency). Build it as a regular Cargo crate after placing
`libtts.a` on the linker path:

```bash
cd bindings/rust
LD_LIBRARY_PATH=../../build/lib cargo build
```

### Go (cgo)

The scaffold in `bindings/go/` uses cgo. Build it after ensuring
`libtts.so` (or `.a`) is on the linker path:

```bash
cd bindings/go
CGO_LDFLAGS="-L$(pwd)/../../build/lib" go build ./...
```

### Node.js (N-API)

The scaffold in `bindings/node/` shows the intended N-API surface.
Build with `node-gyp`:

```bash
cd bindings/node
node-gyp configure
node-gyp build
```

(A full npm package `@tts/sdk` will be published separately.)

---

## 6. Cross-platform notes

| Platform | Status                              |
|----------|-------------------------------------|
| Linux x86_64 | ✅ Tested, ships pre-built      |
| Linux arm64  | ✅ Should work; not CI-tested    |
| macOS arm64  | ✅ Should work; not CI-tested    |
| macOS x86_64 | ✅ Should work; not CI-tested    |
| Windows x64 (MSVC) | ✅ Should work; not CI-tested |
| WASI / Emscripten | ✅ Disable `TTS_USE_MMAP`     |

zstd's hand-written x86 ASM is disabled via `ZSTD_DISABLE_ASM=1` for
portability; the portable C fallback is used instead.

---

## 7. License & attribution

TTS format designed by [Lotus Lab](https://lotuschain.org) — "free for
all forever, just credit us forever :)". This C++ SDK is a clean-room
implementation of the v3.0 specification.

---

## 8. v3.5 additions

### 8.1 TUI binary (`tts-tui`)

When `TTS_BUILD_TUI=ON` (default), the build produces three binaries:

| Binary           | Description                                  |
|------------------|----------------------------------------------|
| `ttsinspect`     | CLI: inspect/validate/query-section/query-page/tui |
| `ttslint`        | Linter                                       |
| `tts-tui`        | Standalone TUI browser                       |

The TUI is also accessible as `ttsinspect tui <file>`.

### 8.2 KMS provider CMake options

The KMS adapters in `include/tts/kms_providers.hpp` are always
available — they don't require any external dependency. The CMake
options `TTS_USE_AWS_KMS` / `TTS_USE_GCP_KMS` / `TTS_USE_VAULT`
exist for future use (when callers want to vendor a transport
implementation alongside the adapter) but the adapters themselves
work without any of these options set — the caller just supplies
their own `IHttpTransport*`.

### 8.3 Node.js binding

See `bindings/node/` for `binding.gyp` + `index.cpp` + `package.json`
+ `index.js`. Build with `node-gyp`:

```bash
cd bindings/node
npm install
node-gyp configure
node-gyp build
node -e "const tts = require('./build/Release/tts.node'); console.log(tts.version());"
```

The Node.js binding covers the core FFI surface:
`version()`, `ttfOpen()`, `ttfGetSection()`, `attfOpen()`,
`attfGetPage()`, `attfGetPageById()`.

### 8.4 Python binding refresh

The Python ctypes binding in `bindings/python/tts/__init__.py` is
unchanged from v3.0. To refresh the shared library:

```bash
cmake -S . -B build-shared -DTTS_BUILD_SHARED=ON -DTTS_BUILD_CLI=OFF -DTTS_BUILD_TUI=OFF -DTTS_BUILD_TESTS=OFF
cmake --build build-shared
cp build-shared/lib/libtts.so.3.5.2 bindings/python/tts/libtts.so
```

