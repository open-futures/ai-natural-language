// =============================================================================
//  src/ttf_writer.cpp — Phase 3
// =============================================================================
#include "tts/ttf_writer.hpp"

#include "tts/sha256.hpp"
#include "tts/utf8_utils.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace tts {

namespace {

// Emit a YAML-quoted string with minimal escaping. Only escapes backslash,
// double-quote, and control bytes. This is sufficient for TTF directory values
// (paths, hashes, encoding names); arbitrary user data lives in the data
// stream, not in the YAML.
[[nodiscard]] std::string yaml_quote(std::string_view v) {
    std::string out;
    out.reserve(v.size() + 2);
    out.push_back('"');
    for (char c : v) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

} // namespace

TtfWriter::TtfWriter() = default;

TtfWriter& TtfWriter::add_section(std::uint32_t section_id, std::string_view content) {
    sections_.push_back(PendingSection{section_id, std::string{content}});
    return *this;
}

TtfWriter& TtfWriter::add_string_tag(std::uint32_t id, std::string name, std::string value) {
    extra_tags_.push_back(PendingTag{id, std::move(name), std::move(value)});
    return *this;
}

std::string TtfWriter::build() const {
    // Step 1: assemble the data stream and record per-section (offset_chars, length_chars, sha256).
    std::string data_stream;
    struct Built { std::uint32_t id; std::uint64_t off; std::uint64_t len; std::string hash; };
    std::vector<Built> built;
    built.reserve(sections_.size());

    std::uint64_t char_cursor = 0;
    for (const auto& s : sections_) {
        const std::uint64_t off = char_cursor;
        const std::uint64_t len = utf8::count_chars(s.content);
        const std::string  hash = crypto::sha256_hex(s.content);
        data_stream.append(s.content);
        built.push_back(Built{s.id, off, len, std::move(hash)});
        char_cursor = off + len;
    }

    // Step 2: emit YAML directory.
    std::string yaml;
    yaml.reserve(512 + sections_.size() * 96);
    yaml += "tfd:\n";
    yaml += "  tfd_id: ";   yaml += std::to_string(tfd_id_);     yaml += "\n";
    yaml += "  next_tfd: "; yaml += std::to_string(next_tfd_);   yaml += "\n";
    yaml += "  encoding: "; yaml += yaml_quote(encoding_);      yaml += "\n";
    yaml += "tags:\n";
    for (const auto& t : extra_tags_) {
        yaml += "  - id: ";     yaml += std::to_string(t.id);
        yaml += "\n    name: "; yaml += yaml_quote(t.name);
        yaml += "\n    value: ";yaml += yaml_quote(t.value);
        yaml += "\n";
    }
    if (!built.empty()) {
        yaml += "  - id: 273\n";
        yaml += "    name: \"SectionLayouts\"\n";
        yaml += "    value:\n";
        for (const auto& b : built) {
            yaml += "      - section: ";        yaml += std::to_string(b.id);
            yaml += "\n        offset_chars: "; yaml += std::to_string(b.off);
            yaml += "\n        length_chars: "; yaml += std::to_string(b.len);
            yaml += "\n        sha256: ";       yaml += yaml_quote(b.hash);
            yaml += "\n";
        }
    }

    // Step 3: assemble the full document.
    constexpr std::string_view kMagic = "[TTF_LE:2.0]\n";
    std::string out;
    out.reserve(kMagic.size() + yaml.size() + data_stream.size() + 64);
    out += kMagic;
    out += "---\n";
    out += yaml;
    out += "---\n";
    out += "[DATA_STREAM_START]\n";
    out += data_stream;
    out += "\n[TTF_EOF]\n";
    return out;
}

TtfStatus
TtfWriter::write_to_file(const std::filesystem::path& path) const {
    std::error_code ec;
    namespace fs = std::filesystem;

    const auto tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(TtfError::IoError);
    }
    const std::string bytes = build();
    if (!bytes.empty()) {
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            out.close();
            (void)fs::remove(tmp, ec);
            return std::unexpected(TtfError::IoError);
        }
    }
    out.close();

    fs::rename(tmp, path, ec);
    if (ec) {
        (void)fs::remove(tmp, ec);
        return std::unexpected(TtfError::IoError);
    }
    return {};
}

} // namespace tts
