// =============================================================================
//  src/ttf_reader.cpp — Phase 3
// =============================================================================
#include "tts/ttf_reader.hpp"

#include "tts/sha256.hpp"
#include "tts/utf8_utils.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <utility>

namespace tts {

namespace {

// Markers — declared as plain string literals so sizeof captures the null.
constexpr std::string_view kMagicHeader      = "[TTF_LE:2.0]\n";
constexpr std::string_view kTfdOpen          = "---\n";
constexpr std::string_view kTfdClose         = "\n---";
constexpr std::string_view kDataStreamMarker = "[DATA_STREAM_START]\n";
constexpr std::string_view kEofMarker        = "\n[TTF_EOF]";

[[nodiscard]] bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() &&
           std::memcmp(s.data(), prefix.data(), prefix.size()) == 0;
}

[[nodiscard]] TtfResult<std::pair<std::size_t, std::size_t>>
locate_tfd(std::string_view buf) noexcept {
    // Find "---\n" after the magic header.
    const std::size_t open_pos = buf.find(kTfdOpen, kMagicHeader.size());
    if (open_pos == std::string_view::npos) {
        return std::unexpected(TtfError::MissingTfdStart);
    }
    const std::size_t yaml_start = open_pos + kTfdOpen.size();
    // Find "\n---" after yaml_start. The YAML content lives in between.
    const std::size_t close_pos = buf.find(kTfdClose, yaml_start);
    if (close_pos == std::string_view::npos) {
        return std::unexpected(TtfError::MissingTfdEnd);
    }
    return std::make_pair(yaml_start, close_pos);
}

[[nodiscard]] TtfResult<YAML::Node>
parse_tfd_yaml(std::string_view yaml_text) noexcept {
    try {
        YAML::Node n = YAML::Load(std::string{yaml_text});  // yaml-cpp needs std::string
        return n;
    } catch (const YAML::Exception&) {
        return std::unexpected(TtfError::MalformedYaml);
    } catch (...) {
        return std::unexpected(TtfError::MalformedYaml);
    }
}

[[nodiscard]] TtfMetadata
extract_metadata(const YAML::Node& root) {
    TtfMetadata m;
    if (auto tfd = root["tfd"]; tfd) {
        if (auto v = tfd["tfd_id"];   v) m.tfd_id   = v.as<std::uint32_t>();
        if (auto v = tfd["next_tfd"]; v) m.next_tfd = v.as<std::uint32_t>();
        if (auto v = tfd["encoding"]; v) m.encoding = v.as<std::string>();
    }
    if (auto tags = root["tags"]; tags && tags.IsSequence()) {
        for (const auto& t : tags) {
            const std::string name = t["name"] ? t["name"].as<std::string>() : std::string{};
            if (name == "SectionLayouts") {
                const YAML::Node& v = t["value"];
                if (v && v.IsSequence()) {
                    for (const auto& sl : v) {
                        SectionLayout s;
                        if (sl["section"])      s.section_id   = sl["section"].as<std::uint32_t>();
                        if (sl["offset_chars"]) s.offset_chars = sl["offset_chars"].as<std::uint64_t>();
                        if (sl["length_chars"]) s.length_chars = sl["length_chars"].as<std::uint64_t>();
                        if (sl["sha256"])       s.sha256_hex   = sl["sha256"].as<std::string>();
                        m.sections.push_back(std::move(s));
                    }
                }
            }
        }
    }
    return m;
}

[[nodiscard]] std::string
lowercase_ascii(std::string s) noexcept {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

} // namespace

// -----------------------------------------------------------------------------
//  open
// -----------------------------------------------------------------------------
TtfResult<TtfReader> TtfReader::open(MemoryView buffer) noexcept {
    if (buffer.empty()) {
        return std::unexpected(TtfError::InvalidArgument);
    }
    const std::string_view buf = buffer.as_string_view();

    if (!starts_with(buf, kMagicHeader)) {
        return std::unexpected(TtfError::InvalidMagicHeader);
    }

    auto tfd_bounds = locate_tfd(buf);
    if (!tfd_bounds) return std::unexpected(tfd_bounds.error());
    const auto [yaml_start, yaml_end] = *tfd_bounds;

    auto root = parse_tfd_yaml(buf.substr(yaml_start, yaml_end - yaml_start));
    if (!root) return std::unexpected(root.error());
    TtfMetadata meta = extract_metadata(*root);

    // After the TFD close ("---"), expect "\n[DATA_STREAM_START]\n"
    // Spec: \n--- then [DATA_STREAM_START]\n
    std::size_t cursor = yaml_end + kTfdClose.size();   // right after "\n---"
    if (cursor >= buf.size() || buf[cursor] != '\n') {
        return std::unexpected(TtfError::MissingDataStreamMarker);
    }
    ++cursor; // skip the newline that terminates the "---" line
    if (!starts_with(buf.substr(cursor), kDataStreamMarker)) {
        return std::unexpected(TtfError::MissingDataStreamMarker);
    }
    cursor += kDataStreamMarker.size();

    const std::size_t data_start = cursor;

    // Find the EOF marker: \n[TTF_EOF]
    // We tolerate a final trailing newline after [TTF_EOF].
    const std::size_t eof_pos = buf.rfind(kEofMarker);
    if (eof_pos == std::string_view::npos || eof_pos < data_start) {
        return std::unexpected(TtfError::MissingEofMarker);
    }
    const std::size_t data_end = eof_pos;

    TtfReader r;
    r.buffer_                  = std::move(buffer);
    r.meta_                    = std::move(meta);
    r.data_stream_             = buf.substr(data_start, data_end - data_start);
    r.data_stream_byte_offset_ = data_start;
    for (std::size_t i = 0; i < r.meta_.sections.size(); ++i) {
        r.id_to_index_[r.meta_.sections[i].section_id] = i;
    }
    return r;
}

TtfResult<TtfReader>
TtfReader::open_file(const std::filesystem::path& path) noexcept {
    auto mv = MemoryView::from_file(path);
    if (!mv) return std::unexpected(TtfError::IoError);
    return open(std::move(*mv));
}

// -----------------------------------------------------------------------------
//  get_section
// -----------------------------------------------------------------------------
TtfResult<std::string_view>
TtfReader::get_section(std::uint32_t section_id) const noexcept {
    const auto it = id_to_index_.find(section_id);
    if (it == id_to_index_.end()) {
        return std::unexpected(TtfError::SectionNotFound);
    }
    const auto& sl = meta_.sections[it->second];

    // Convert character offsets to byte offsets.
    auto b_start = utf8::char_offset_to_byte(data_stream_, sl.offset_chars);
    if (!b_start) return std::unexpected(b_start.error());
    auto b_end = utf8::char_offset_to_byte(data_stream_, sl.offset_chars + sl.length_chars);
    if (!b_end) return std::unexpected(b_end.error());

    if (*b_start > data_stream_.size() || *b_end > data_stream_.size() || *b_end < *b_start) {
        return std::unexpected(TtfError::LengthOutOfRange);
    }

    const std::string_view section_bytes = data_stream_.substr(*b_start, *b_end - *b_start);

    // Hash verification.
    if (!sl.sha256_hex.empty()) {
        const std::string actual_hex = crypto::sha256_hex(section_bytes);
        // Constant-time comparison: prevents timing oracle for hash recovery.
        // Both strings are case-normalised first so case differences don't leak
        // through as length-equivalent mismatches.
        const std::string actual_norm  = lowercase_ascii(actual_hex);
        const std::string expected_norm = lowercase_ascii(sl.sha256_hex);
        if (!crypto::constant_time_equals_hex(actual_norm, expected_norm)) {
            return std::unexpected(TtfError::HashMismatch);
        }
    }

    return section_bytes;
}

std::vector<std::uint32_t> TtfReader::section_ids() const {
    std::vector<std::uint32_t> out;
    out.reserve(meta_.sections.size());
    for (const auto& s : meta_.sections) out.push_back(s.section_id);
    return out;
}

TtfResult<std::uint32_t> TtfReader::validate_all() const noexcept {
    for (const auto& s : meta_.sections) {
        auto r = get_section(s.section_id);
        if (!r) {
            if (r.error() == TtfError::SectionNotFound) continue;
            return std::unexpected(r.error());
        }
    }
    return std::uint32_t{0};
}

// -----------------------------------------------------------------------------
//  v3.0 — chain traversal (next_tfd)
// -----------------------------------------------------------------------------
TtfResult<std::vector<TtfReader>>
TtfReader::follow_chain(const std::filesystem::path& base_path,
                        std::string_view chain_suffix,
                        std::size_t max_depth) const {
    std::vector<TtfReader> chain;
    chain.reserve(4);

    // We don't include `this` in the returned vector — callers already
    // hold this reader. We only return the *subsequent* readers.
    std::uint32_t next_id = meta_.next_tfd;
    if (next_id == 0) return chain;

    std::filesystem::path current = base_path;
    for (std::size_t depth = 0; depth < max_depth && next_id != 0; ++depth) {
        // Construct the chained path: <base><chain_suffix>
        // (e.g. "doc.ttf" + ".next.ttf" -> "doc.next.ttf")
        current = std::filesystem::path{
            std::string{base_path.string()} + std::string{chain_suffix}
        };

        auto r = TtfReader::open_file(current);
        if (!r) {
            // Chained file missing or corrupt — return what we have so far.
            // The caller can decide whether to treat this as fatal.
            break;
        }
        next_id = r->metadata().next_tfd;
        chain.push_back(std::move(*r));

        // All chain entries use the same suffix, so the next iteration will
        // try the same path again. To support multi-hop chains, we'd need
        // a per-hop suffix or a directory listing; for now we stop at one hop
        // to avoid infinite loops on cyclic chains.
        break;
    }
    return chain;
}

TtfResult<std::string_view>
TtfReader::get_section_in_chain(std::uint32_t section_id,
                                const std::vector<TtfReader>& chain) const noexcept {
    // Try local first.
    if (auto local_r = get_section(section_id)) return *local_r;
    else if (local_r.error() != TtfError::SectionNotFound)
        return std::unexpected(local_r.error());
    // Walk the chain.
    for (const auto& rdr : chain) {
        if (auto r2 = rdr.get_section(section_id)) return *r2;
        else if (r2.error() != TtfError::SectionNotFound)
            return std::unexpected(r2.error());
    }
    return std::unexpected(TtfError::SectionNotFound);
}

} // namespace tts
