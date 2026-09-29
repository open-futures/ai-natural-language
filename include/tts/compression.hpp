// =============================================================================
//  include/tts/compression.hpp
//  Phase M2 — Per-section compression interface.
//
//  Spec (TTF v3.0): SectionLayout gains an optional `compression` field:
//    - section: 1
//      offset_chars: 0
//      length_chars: 1234
//      sha256: "..."
//      compression: "zstd"    # or "none" (default if absent)
//
//  The data stream stores the COMPRESSED bytes. The reader transparently
//  decompresses on get_section(). The SHA-256 is computed over the
//  PLAINTEXT — so integrity survives compression.
//
//  For the SDK, we ship:
//    - "none" — identity
//    - "zstd" — via FetchContent(zstd); falls back to identity if the
//              build was configured with -DTTS_USE_ZSTD=OFF
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tts::compression {

enum class Algorithm : std::uint8_t {
    None = 0,
    Zstd = 1,
};

[[nodiscard]] std::string_view to_string(Algorithm a) noexcept;
[[nodiscard]] TtfResult<Algorithm> from_string(std::string_view name) noexcept;

/// Compress `input` into `output`. Returns the number of bytes written.
[[nodiscard]] TtfResult<std::vector<std::uint8_t>>
compress(Algorithm algo, std::span<const std::uint8_t> input, int level = 3);

/// Decompress `input` into `output`. Returns the decompressed bytes.
[[nodiscard]] TtfResult<std::vector<std::uint8_t>>
decompress(Algorithm algo, std::span<const std::uint8_t> input);

/// Convenience overloads for string_view.
[[nodiscard]] TtfResult<std::string>
compress_string(Algorithm algo, std::string_view input, int level = 3);

[[nodiscard]] TtfResult<std::string>
decompress_string(Algorithm algo, std::string_view input);

} // namespace tts::compression
