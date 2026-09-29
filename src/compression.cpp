// =============================================================================
//  src/compression.cpp — M2
//  Uses zstd via FetchContent if TTS_USE_ZSTD=ON, otherwise identity fallback.
// =============================================================================
#include "tts/compression.hpp"

#include <cstring>

#if defined(TTS_USE_ZSTD) && TTS_USE_ZSTD
#  include <zstd.h>
#  define TTS_HAS_ZSTD 1
#else
#  define TTS_HAS_ZSTD 0
#endif

namespace tts::compression {

std::string_view to_string(Algorithm a) noexcept {
    switch (a) {
        case Algorithm::None: return "none";
        case Algorithm::Zstd: return "zstd";
    }
    return "none";
}

TtfResult<Algorithm> from_string(std::string_view name) noexcept {
    if (name == "none" || name.empty()) return Algorithm::None;
    if (name == "zstd")                  return Algorithm::Zstd;
    return std::unexpected(TtfError::InvalidArgument);
}

TtfResult<std::vector<std::uint8_t>>
compress(Algorithm algo, std::span<const std::uint8_t> input, int level) {
    if (algo == Algorithm::None) {
        return std::vector<std::uint8_t>{input.begin(), input.end()};
    }
#if TTS_HAS_ZSTD
    if (algo == Algorithm::Zstd) {
        const std::size_t bound = ZSTD_compressBound(input.size());
        std::vector<std::uint8_t> out(bound);
        const std::size_t n = ZSTD_compress(
            out.data(), out.size(),
            input.data(), input.size(),
            level);
        if (ZSTD_isError(n)) {
            return std::unexpected(TtfError::IoError);
        }
        out.resize(n);
        return out;
    }
#endif
    // Zstd requested but not built in — fail loudly.
    return std::unexpected(TtfError::InvalidArgument);
}

TtfResult<std::vector<std::uint8_t>>
decompress(Algorithm algo, std::span<const std::uint8_t> input) {
    if (algo == Algorithm::None) {
        return std::vector<std::uint8_t>{input.begin(), input.end()};
    }
#if TTS_HAS_ZSTD
    if (algo == Algorithm::Zstd) {
        const std::uint64_t frame_size = ZSTD_getFrameContentSize(input.data(), input.size());
        if (frame_size == ZSTD_CONTENTSIZE_ERROR ||
            frame_size == ZSTD_CONTENTSIZE_UNKNOWN) {
            return std::unexpected(TtfError::IoError);
        }
        std::vector<std::uint8_t> out(static_cast<std::size_t>(frame_size));
        const std::size_t n = ZSTD_decompress(
            out.data(), out.size(),
            input.data(), input.size());
        if (ZSTD_isError(n)) {
            return std::unexpected(TtfError::IoError);
        }
        out.resize(n);
        return out;
    }
#endif
    return std::unexpected(TtfError::InvalidArgument);
}

TtfResult<std::string>
compress_string(Algorithm algo, std::string_view input, int level) {
    auto r = compress(algo,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size()},
        level);
    if (!r) return std::unexpected(r.error());
    return std::string{reinterpret_cast<const char*>(r->data()), r->size()};
}

TtfResult<std::string>
decompress_string(Algorithm algo, std::string_view input) {
    auto r = decompress(algo,
        std::span<const std::uint8_t>{
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
    if (!r) return std::unexpected(r.error());
    return std::string{reinterpret_cast<const char*>(r->data()), r->size()};
}

} // namespace tts::compression
