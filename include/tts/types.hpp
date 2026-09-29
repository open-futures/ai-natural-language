// =============================================================================
//  include/tts/types.hpp
//  Phase 2 — Core error types and result aliases.
//
//  All public SDK APIs return std::expected<T, E> where E is one of the
//  strongly-typed error enums declared here. Never throw across the public
//  API boundary.
// =============================================================================
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace tts {

// -----------------------------------------------------------------------------
//  TTF errors
// -----------------------------------------------------------------------------
enum class TtfError : std::uint8_t {
    Ok = 0,
    InvalidMagicHeader,        // File does not start with [TTF_LE:2.0]\n
    MissingTfdStart,           // Could not find the opening "---\n"
    MissingTfdEnd,             // Could not find the closing "\n---"
    MalformedYaml,             // yaml-cpp rejected the TFD block
    MissingDataStreamMarker,   // [DATA_STREAM_START]\n not present
    MissingEofMarker,          // [TTF_EOF] not present
    SectionNotFound,           // No SectionLayout with that section id
    OffsetOutOfRange,          // offset_chars exceeds data stream length
    LengthOutOfRange,          // length_chars pushes past data stream end
    InvalidUtf8Sequence,       // data stream contains an invalid UTF-8 lead byte
    HashMismatch,              // SHA-256 of extracted slice != directory hash
    IoError,                   // underlying read failed (mmap or file read)
    InvalidArgument,           // caller passed a bad parameter
    NotInitialized,            // reader was used before open()
};

// -----------------------------------------------------------------------------
//  ATTF errors
// -----------------------------------------------------------------------------
enum class AttfError : std::uint8_t {
    Ok = 0,
    InvalidMagicHeader,        // First byte is not \x01 or payload is not ATTF_MEM:2.0
    InvalidVersion,            // Header version string malformed
    MissingDataStream,         // No \x02 (STX) found
    InvalidIndexRow,           // Index row does not match 400:/401: schema
    InvalidPageHeader,         // P{id}:{addr} header malformed
    PageNotFound,               // hex address not present in index
    DuplicateAddress,           // writer asked to insert an existing address
    AddressParseError,         // 0x... hex literal malformed
    InvalidByteSequence,       // stray control byte in payload
    IoError,
    InvalidArgument,
    NotInitialized,
};

// -----------------------------------------------------------------------------
//  Error formatting (used by both C++ consumers and the C FFI layer)
// -----------------------------------------------------------------------------
[[nodiscard]] const char* to_string(TtfError e) noexcept;
[[nodiscard]] const char* to_string(AttfError e) noexcept;

// Convenience: convert to a stable integer for FFI.
[[nodiscard]] constexpr int to_int(TtfError e) noexcept   { return static_cast<int>(e); }
[[nodiscard]] constexpr int to_int(AttfError e) noexcept  { return static_cast<int>(e); }

// -----------------------------------------------------------------------------
//  Result aliases — std::expected<T, Error>
// -----------------------------------------------------------------------------
template <typename T> using TtfResult  = std::expected<T, TtfError>;
template <typename T> using AttfResult = std::expected<T, AttfError>;

// A non-data result is just std::expected<void, E>.
using TtfStatus  = std::expected<void, TtfError>;
using AttfStatus = std::expected<void, AttfError>;

} // namespace tts
