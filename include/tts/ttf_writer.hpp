// =============================================================================
//  include/tts/ttf_writer.hpp
//  Phase 3 — TTF builder.
//
//  Usage:
//    tts::TtfWriter w;
//    w.set_tfd_id(1).set_next_tfd(0);
//    w.add_section(1, "Hello world");
//    w.add_section(2, "Second section");
//    auto bytes = w.build();              // std::string
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tts {

class TtfWriter {
public:
    TtfWriter();
    ~TtfWriter() = default;

    TtfWriter(TtfWriter&&) noexcept            = default;
    TtfWriter& operator=(TtfWriter&&) noexcept = default;
    TtfWriter(const TtfWriter&)                = delete;
    TtfWriter& operator=(const TtfWriter&)      = delete;

    // --- fluent setters -------------------------------------------------------
    TtfWriter& set_tfd_id(std::uint32_t id) noexcept        { tfd_id_ = id; return *this; }
    TtfWriter& set_next_tfd(std::uint32_t id) noexcept      { next_tfd_ = id; return *this; }
    TtfWriter& set_encoding(std::string enc) noexcept        { encoding_ = std::move(enc); return *this; }

    // --- content --------------------------------------------------------------
    /// Append a section. The content's UTF-8 byte layout is preserved.
    /// Offset/length in chars and SHA-256 are computed automatically.
    TtfWriter& add_section(std::uint32_t section_id, std::string_view content);

    /// Insert a free-form string tag (id, name, scalar value).
    TtfWriter& add_string_tag(std::uint32_t id,
                              std::string  name,
                              std::string  value);

    // --- emission -------------------------------------------------------------
    /// Build the full TTF document as a UTF-8 string.
    [[nodiscard]] std::string build() const;

    /// Build and write to disk atomically (writes to .tmp, then renames).
    [[nodiscard]] TtfStatus write_to_file(const std::filesystem::path& path) const;

private:
    struct PendingSection {
        std::uint32_t id;
        std::string   content;
    };
    struct PendingTag {
        std::uint32_t id;
        std::string   name;
        std::string   value;
    };

    std::uint32_t             tfd_id_   = 1;
    std::uint32_t             next_tfd_ = 0;
    std::string               encoding_ = "UTF-8";
    std::vector<PendingSection> sections_;
    std::vector<PendingTag>    extra_tags_;
};

} // namespace tts
