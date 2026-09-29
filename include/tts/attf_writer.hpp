// =============================================================================
//  include/tts/attf_writer.hpp
//  Phase 4 — ATTF builder.
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tts {

class AttfWriter {
public:
    AttfWriter();
    ~AttfWriter() = default;
    AttfWriter(AttfWriter&&) noexcept            = default;
    AttfWriter& operator=(AttfWriter&&) noexcept = default;
    AttfWriter(const AttfWriter&)                = delete;
    AttfWriter& operator=(const AttfWriter&)      = delete;

    // --- fluent setters -------------------------------------------------------
    AttfWriter& set_version(std::string v) noexcept   { version_ = std::move(v); return *this; }
    AttfWriter& add_setting(std::string key, std::string value);

    /// Append a memory page.
    ///   page_id    — numeric page id (e.g. 1, 2, 3…)
    ///   type       — semantic class (sys_prompt | semantic | episodic | …)
    ///   hex_address— virtual hex address string, e.g. "0x00A1"
    ///   priority   — float in [0,10], higher = more important
    ///   content    — UTF-8 page body (may contain any bytes except \x03)
    ///   keywords   — optional semantic-index keywords (Tag 401)
    AttfWriter& add_page(std::uint32_t page_id,
                         std::string   type,
                         std::string   hex_address,
                         double        priority,
                         std::string_view content,
                         std::vector<std::string> keywords = {});

    // --- emission -------------------------------------------------------------
    [[nodiscard]] std::string build() const;
    [[nodiscard]] AttfStatus  write_to_file(const std::filesystem::path& path) const;

private:
    struct PendingPage {
        std::uint32_t              page_id;
        std::string                type;
        std::string                hex_address;
        double                     priority;
        std::string                content;
        std::vector<std::string>   keywords;
    };

    std::string                 version_  = "2.0";
    std::vector<std::pair<std::string, std::string>> settings_;
    std::vector<PendingPage>    pages_;
};

} // namespace tts
