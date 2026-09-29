// =============================================================================
//  include/tts/attf_reader.hpp
//  Phase 4 — Zero-copy ATTF parser.
//
//  ATTF uses single-byte control characters:
//    \x01 (SOH)  start of heading / variant
//    \x04 (EOT)  global settings
//    \x03 (ETX)  end of entry / row delimiter (index rows AND page headers)
//    \x02 (STX)  start of data stream
//
//  Layout:
//    \x01ATTF_MEM:<version>\n
//    \x04<key>:<value>|<key>:<value>\n          (one or more settings lines)
//    \x03400:<id>,<type>,<addr>,<priority>\n     (page directory entries)
//    \x03401:<addr>,["kw1","kw2"]\n              (semantic index entries)
//    \x02\n                                       (start of data stream)
//    \x03P<id>:<addr>\n<content>\n                (one page per ETX block)
//    \x03\n                                       (trailing terminator)
// =============================================================================
#pragma once

#include "tts/memory_view.hpp"
#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tts {

// -----------------------------------------------------------------------------
//  Parsed model
// -----------------------------------------------------------------------------
struct AttfPageEntry {
    std::uint32_t page_id   = 0;
    std::string   type;
    std::string   hex_address;
    double        priority  = 0.0;
};

struct AttfSemanticEntry {
    std::string                 hex_address;
    std::vector<std::string>   keywords;
};

struct AttfHeader {
    std::string version = "2.0";          // e.g. "2.0"
    // Free-form key|value pairs from \x04 settings, in order of appearance.
    // Common keys: mem_zone, last_sync.
    std::vector<std::pair<std::string, std::string>> settings;

    [[nodiscard]] std::string get_setting(std::string_view key) const;
};

// -----------------------------------------------------------------------------
//  AttfReader
// -----------------------------------------------------------------------------
class AttfReader {
public:
    AttfReader() = default;
    ~AttfReader() = default;
    AttfReader(AttfReader&&) noexcept            = default;
    AttfReader& operator=(AttfReader&&) noexcept = default;
    AttfReader(const AttfReader&)                = delete;
    AttfReader& operator=(const AttfReader&)      = delete;

    /// Open from an already-loaded buffer (zero-copy).
    [[nodiscard]] static AttfResult<AttfReader> open(MemoryView buffer) noexcept;

    /// Convenience: mmap-or-read a file and open it.
    [[nodiscard]] static AttfResult<AttfReader>
    open_file(const std::filesystem::path& path) noexcept;

    /// Parsed header.
    [[nodiscard]] const AttfHeader& header() const noexcept { return header_; }

    /// Page directory (Tag 400 rows).
    [[nodiscard]] const std::vector<AttfPageEntry>& directory() const noexcept {
        return directory_;
    }

    /// Semantic index (Tag 401 rows).
    [[nodiscard]] const std::vector<AttfSemanticEntry>& semantic_index() const noexcept {
        return semantic_index_;
    }

    /// Zero-copy page content lookup by hex address (e.g. "0x00A1").
    /// The returned view aliases the underlying MemoryView and is valid only
    /// as long as the AttfReader (and its MemoryView) is alive.
    [[nodiscard]] AttfResult<std::string_view>
    get_page(std::string_view hex_address) const noexcept;

    /// Same as get_page but takes a numeric page_id.
    [[nodiscard]] AttfResult<std::string_view>
    get_page_by_id(std::uint32_t page_id) const noexcept;

    /// All known page addresses (in directory order).
    [[nodiscard]] std::vector<std::string> page_addresses() const;

private:
    MemoryView     buffer_;
    AttfHeader     header_;
    std::vector<AttfPageEntry>    directory_;
    std::vector<AttfSemanticEntry> semantic_index_;
    std::unordered_map<std::string, std::string_view> address_to_content_;
    std::unordered_map<std::uint32_t, std::string>     id_to_address_;
    // Backing store for pages that contained escape sequences (v3.0).
    // Unescaped pages can alias the underlying buffer directly; escaped
    // pages must be materialised because their decoded form differs from
    // the on-disk bytes.
    std::vector<std::string> escaped_pages_;
};

} // namespace tts
