// =============================================================================
//  include/tts/utf8_utils.hpp
//  Phase 2 — UTF-8 character arithmetic.
//
//  TTF stores offsets and lengths in *characters* (code points), not bytes.
//  The data stream is well-formed UTF-8, so one code point is 1–4 bytes.
//  These helpers convert between character counts and byte counts WITHOUT
//  allocating — they only walk the byte stream.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "tts/types.hpp"

namespace tts::utf8 {

// -----------------------------------------------------------------------------
//  Lead-byte classification (RFC 3629)
// -----------------------------------------------------------------------------
[[nodiscard]] constexpr bool is_continuation(std::uint8_t b) noexcept {
    return (b & 0xC0u) == 0x80u;
}

[[nodiscard]] constexpr int lead_byte_length(std::uint8_t b) noexcept {
    if ((b & 0x80u) == 0x00u) return 1;          // U+0000..U+007F
    if ((b & 0xE0u) == 0xC0u) return 2;          // U+0080..U+07FF
    if ((b & 0xF0u) == 0xE0u) return 3;          // U+0800..U+FFFF
    if ((b & 0xF8u) == 0xF0u) return 4;          // U+10000..U+10FFFF
    return 0;                                    // invalid lead byte
}

// -----------------------------------------------------------------------------
//  count_chars — code-point count of a UTF-8 byte range.
//
//  Returns the number of code points; malformed continuations are tolerated
//  by falling back to counting the stray byte as a single code point (so the
//  count matches what a lenient decoder would emit). Use validate() if you
//  need strict rejection.
// -----------------------------------------------------------------------------
[[nodiscard]] inline std::size_t
count_chars(std::string_view s) noexcept {
    std::size_t n = 0;
    std::size_t i = 0;
    const std::size_t sz = s.size();
    while (i < sz) {
        const int len = lead_byte_length(static_cast<std::uint8_t>(s[i]));
        if (len == 0) { ++n; ++i; continue; }    // stray byte: count as 1
        ++n;
        i += static_cast<std::size_t>(len);
    }
    return n;
}

[[nodiscard]] inline std::size_t
count_chars(std::span<const std::uint8_t> bytes) noexcept {
    return count_chars(std::string_view{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

// -----------------------------------------------------------------------------
//  char_offset_to_byte — turn a character index into a byte index.
//
//  Semantics:
//    char_offset_to_byte(s, 0)   == 0
//    char_offset_to_byte(s, n)   == byte offset of the (n+1)-th code point
//    char_offset_to_byte(s, cnt) == s.size()  (one-past-end, allowed)
//
//  Returns TtfError::InvalidUtf8Sequence if the stream contains an invalid
//  lead byte before reaching the requested offset, and
//  TtfError::OffsetOutOfRange if char_offset exceeds the number of code points.
// -----------------------------------------------------------------------------
[[nodiscard]] inline TtfResult<std::size_t>
char_offset_to_byte(std::string_view s, std::size_t char_offset) noexcept {
    std::size_t i = 0;
    std::size_t c = 0;
    const std::size_t sz = s.size();
    while (c < char_offset) {
        if (i >= sz) {
            // Allow one-past-end only when char_offset == count_chars(s).
            // We can't distinguish cheaply without walking; bail.
            if (c == char_offset) return i;
            return std::unexpected(TtfError::OffsetOutOfRange);
        }
        const int len = lead_byte_length(static_cast<std::uint8_t>(s[i]));
        if (len == 0) return std::unexpected(TtfError::InvalidUtf8Sequence);
        if (i + static_cast<std::size_t>(len) > sz)
            return std::unexpected(TtfError::InvalidUtf8Sequence);
        // Validate continuation bytes while we're here.
        for (int k = 1; k < len; ++k) {
            if (!is_continuation(static_cast<std::uint8_t>(s[i + k])))
                return std::unexpected(TtfError::InvalidUtf8Sequence);
        }
        i += static_cast<std::size_t>(len);
        ++c;
    }
    return i;
}

// -----------------------------------------------------------------------------
//  substr_chars — return a substring by (char_offset, char_length).
//
//  Zero allocation: returns a std::string_view into the original buffer.
// -----------------------------------------------------------------------------
[[nodiscard]] inline TtfResult<std::string_view>
substr_chars(std::string_view s,
             std::size_t offset_chars,
             std::size_t length_chars) noexcept {
    auto start = char_offset_to_byte(s, offset_chars);
    if (!start) return std::unexpected(start.error());
    auto end = char_offset_to_byte(s, offset_chars + length_chars);
    if (!end) return std::unexpected(end.error());
    return s.substr(*start, *end - *start);
}

} // namespace tts::utf8
