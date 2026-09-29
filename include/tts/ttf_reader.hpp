// =============================================================================
//  include/tts/ttf_reader.hpp
//  Phase 3 — Zero-copy TTF parser.
//
//  Lifetime: the underlying MemoryView (and any mmap region it owns) must
//  outlive every string_view returned by get_section().
// =============================================================================
#pragma once

#include "tts/memory_view.hpp"
#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tts {

// -----------------------------------------------------------------------------
//  Parsed model
// -----------------------------------------------------------------------------
struct SectionLayout {
    std::uint32_t section_id    = 0;
    std::uint64_t offset_chars  = 0;
    std::uint64_t length_chars  = 0;
    std::string   sha256_hex;        // 64 lowercase hex chars
};

struct TtfTag {
    std::uint32_t id = 0;
    std::string   name;
    // We retain the YAML node for tag values that are not SectionLayouts
    // so callers can introspect arbitrary tags. This is opaque-typed to
    // keep yaml-cpp out of the public header surface when the SDK is
    // consumed without YAML — but since yaml-cpp is PUBLIC-linked, we
    // allow direct exposure for callers that want it.
};

struct TtfMetadata {
    std::uint32_t             tfd_id   = 0;
    std::uint32_t             next_tfd = 0;
    std::string               encoding = "UTF-8";
    std::vector<SectionLayout> sections;
};

// -----------------------------------------------------------------------------
//  TtfReader
// -----------------------------------------------------------------------------
class TtfReader {
public:
    TtfReader() = default;
    ~TtfReader() = default;

    TtfReader(TtfReader&&) noexcept            = default;
    TtfReader& operator=(TtfReader&&) noexcept = default;
    TtfReader(const TtfReader&)                = delete;
    TtfReader& operator=(const TtfReader&)      = delete;

    /// Open from an already-loaded buffer (zero-copy).
    [[nodiscard]] static TtfResult<TtfReader> open(MemoryView buffer) noexcept;

    /// Convenience: mmap-or-read a file and open it.
    [[nodiscard]] static TtfResult<TtfReader>
    open_file(const std::filesystem::path& path) noexcept;

    /// Parsed directory.
    [[nodiscard]] const TtfMetadata& metadata() const noexcept { return meta_; }

    /// Whole data stream as a string_view. Zero-copy.
    [[nodiscard]] std::string_view data_stream() const noexcept { return data_stream_; }

    /// Byte offset where the data stream begins in the underlying buffer.
    [[nodiscard]] std::size_t data_stream_byte_offset() const noexcept {
        return data_stream_byte_offset_;
    }

    /// Fetch a section by id. Verifies SHA-256 before returning the view.
    /// The returned view aliases the underlying MemoryView and is only valid
    /// as long as the TtfReader (and its MemoryView) is alive.
    [[nodiscard]] TtfResult<std::string_view>
    get_section(std::uint32_t section_id) const noexcept;

    /// List of section ids in directory order.
    [[nodiscard]] std::vector<std::uint32_t> section_ids() const;

    /// Check that every section's hash matches its content.
    /// Returns the first mismatching section id (or Ok if all pass).
    [[nodiscard]] TtfResult<std::uint32_t> validate_all() const noexcept;

    /// v3.0 — Follow the `next_tfd` chain.
    ///
    /// If this reader's metadata() has next_tfd != 0, attempts to open the
    /// chained file at `base_path + ".next.ttf"` (configurable via
    /// `chain_suffix`). Returns an empty vector if there is no chain.
    ///
    /// Each TtfReader in the returned vector owns its own MemoryView.
    /// Sections are looked up across the whole chain by get_section_in_chain().
    [[nodiscard]] TtfResult<std::vector<TtfReader>>
    follow_chain(const std::filesystem::path& base_path,
                 std::string_view chain_suffix = ".next.ttf",
                 std::size_t max_depth = 32) const;

    /// v3.0 — Look up a section across this reader AND any chained readers.
    /// Returns the first match (in chain order).
    [[nodiscard]] TtfResult<std::string_view>
    get_section_in_chain(std::uint32_t section_id,
                         const std::vector<TtfReader>& chain) const noexcept;

private:
    MemoryView     buffer_;
    TtfMetadata    meta_;
    std::string_view data_stream_;
    std::size_t    data_stream_byte_offset_ = 0;
    std::unordered_map<std::uint32_t, std::size_t> id_to_index_;
};

} // namespace tts
