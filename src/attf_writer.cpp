// =============================================================================
//  src/attf_writer.cpp — Phase 4
// =============================================================================
#include "tts/attf_writer.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

namespace tts {

namespace {

// Control bytes — keep as integers to avoid signed-char arithmetic surprises.
constexpr char kSoh = '\x01';
constexpr char kStx = '\x02';
constexpr char kEtx = '\x03';
constexpr char kEot = '\x04';

// ATTF v3.0 escape sequence: \x1b\x03 represents a literal ETX byte in
// page content. The writer escapes any \x03 in user content to \x1b\x03
// so the reader can include binary data with embedded ETX bytes.
constexpr char kEsc = '\x1b';

[[nodiscard]] std::string escape_attf_content(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (c == kEtx) { out.push_back(kEsc); out.push_back(kEtx); }
        else if (c == kEsc) { out.push_back(kEsc); out.push_back(kEsc); }
        else out.push_back(c);
    }
    return out;
}

[[nodiscard]] std::string format_double(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return std::string{buf};
}

/// Emit a minimal JSON string array: ["a","b","c"]
[[nodiscard]] std::string
to_json_array(const std::vector<std::string>& items) {
    std::string out;
    out.reserve(items.size() * 8 + 4);
    out.push_back('[');
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out.push_back(',');
        out.push_back('"');
        for (char c : items[i]) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                default:   out.push_back(c);
            }
        }
        out.push_back('"');
    }
    out.push_back(']');
    return out;
}

} // namespace

AttfWriter::AttfWriter() = default;

AttfWriter& AttfWriter::add_setting(std::string key, std::string value) {
    settings_.emplace_back(std::move(key), std::move(value));
    return *this;
}

AttfWriter& AttfWriter::add_page(std::uint32_t page_id,
                                 std::string   type,
                                 std::string   hex_address,
                                 double        priority,
                                 std::string_view content,
                                 std::vector<std::string> keywords) {
    pages_.push_back(PendingPage{
        page_id,
        std::move(type),
        std::move(hex_address),
        priority,
        std::string{content},
        std::move(keywords)
    });
    return *this;
}

std::string AttfWriter::build() const {
    // Estimate size to avoid reallocations.
    std::size_t est = 64;
    for (const auto& s : settings_) est += s.first.size() + s.second.size() + 8;
    for (const auto& p : pages_) {
        est += 64 + p.type.size() + p.hex_address.size() + p.content.size();
        for (const auto& k : p.keywords) est += k.size() + 4;
    }
    std::string out;
    out.reserve(est);

    // 1) Header
    out.push_back(kSoh);
    out += "ATTF_MEM:";
    out += version_;
    out.push_back('\n');

    // 2) Settings (\x04 key|value\n)
    if (!settings_.empty()) {
        out.push_back(kEot);
        for (std::size_t i = 0; i < settings_.size(); ++i) {
            if (i) out.push_back('|');
            out += settings_[i].first;
            out.push_back(':');
            out += settings_[i].second;
        }
        out.push_back('\n');
    }

    // 3) Index rows
    for (const auto& p : pages_) {
        out.push_back(kEtx);
        out += "400:";
        out += std::to_string(p.page_id);
        out.push_back(',');
        out += p.type;
        out.push_back(',');
        out += p.hex_address;
        out.push_back(',');
        out += format_double(p.priority);
        out.push_back('\n');
    }
    for (const auto& p : pages_) {
        if (p.keywords.empty()) continue;
        out.push_back(kEtx);
        out += "401:";
        out += p.hex_address;
        out.push_back(',');
        out += to_json_array(p.keywords);
        out.push_back('\n');
    }

    // 4) Data stream start
    out.push_back(kStx);
    out.push_back('\n');

    // 5) Pages
    for (const auto& p : pages_) {
        out.push_back(kEtx);
        out += "P";
        out += std::to_string(p.page_id);
        out.push_back(':');
        out += p.hex_address;
        out.push_back('\n');
        // Escape \x03 and \x1b in content so the byte stream is unambiguous.
        out += escape_attf_content(p.content);
        // Page body MUST end with a newline so the parser can strip it
        // cleanly before the next \x03.
        if (p.content.empty() || p.content.back() != '\n') {
            out.push_back('\n');
        }
    }

    // 6) Trailing terminator
    out.push_back(kEtx);
    out.push_back('\n');

    return out;
}

AttfStatus
AttfWriter::write_to_file(const std::filesystem::path& path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto tmp = path.string() + ".tmp";
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return std::unexpected(AttfError::IoError);
    const std::string bytes = build();
    if (!bytes.empty()) {
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!f) { f.close(); (void)fs::remove(tmp, ec); return std::unexpected(AttfError::IoError); }
    }
    f.close();
    fs::rename(tmp, path, ec);
    if (ec) { (void)fs::remove(tmp, ec); return std::unexpected(AttfError::IoError); }
    return {};
}

} // namespace tts
