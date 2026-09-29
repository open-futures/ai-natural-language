// =============================================================================
//  src/attf_reader.cpp — Phase 4
//
//  All scanning operates on raw uint8_t bytes to guarantee exact 1-byte
//  delimiter matching — even if the payload contains multibyte UTF-8 whose
//  continuation bytes coincidentally equal one of our control chars (they
//  can't, since UTF-8 continuation bytes have the high bit set and our
//  control chars are all < 0x20, but we still observe the spec's mandate).
// =============================================================================
#include "tts/attf_reader.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>

namespace tts {

namespace {

// Control bytes — defined as integers so we never confuse them with char
// arithmetic on platforms where char is signed.
constexpr std::uint8_t kSoh = 0x01;  // start of heading
constexpr std::uint8_t kStx = 0x02;  // start of data stream
constexpr std::uint8_t kEtx = 0x03;  // end of entry / row delimiter
constexpr std::uint8_t kEot = 0x04;  // global settings
constexpr std::uint8_t kEsc = 0x1b;  // ATTF v3.0 escape introducer

[[nodiscard]] std::string_view
as_sv(std::span<const std::uint8_t> b) noexcept {
    return std::string_view{reinterpret_cast<const char*>(b.data()), b.size()};
}

/// Find the next occurrence of `needle` starting at `from`.
[[nodiscard]] std::size_t
find_byte(std::span<const std::uint8_t> b, std::uint8_t needle, std::size_t from) noexcept {
    if (from >= b.size()) return std::string::npos;
    const auto* p = static_cast<const std::uint8_t*>(
        std::memchr(b.data() + from, needle, b.size() - from));
    return p ? static_cast<std::size_t>(p - b.data()) : std::string::npos;
}

/// Find the next newline starting at `from`. Returns npos if none.
[[nodiscard]] std::size_t
find_newline(std::span<const std::uint8_t> b, std::size_t from) noexcept {
    return find_byte(b, '\n', from);
}

/// Read a logical line: from `cursor`, advance to the next \n (exclusive).
/// Returns the line content (without trailing \n) and updates `cursor` to
/// point one byte past the \n.
[[nodiscard]] std::string_view
read_line(std::span<const std::uint8_t> b, std::size_t& cursor) noexcept {
    const std::size_t nl = find_newline(b, cursor);
    std::string_view line;
    if (nl == std::string::npos) {
        line = as_sv(b).substr(cursor);
        cursor = b.size();
    } else {
        line = as_sv(b).substr(cursor, nl - cursor);
        cursor = nl + 1;
    }
    return line;
}

/// Parse "key:value|key:value" into a vector of pairs.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
parse_kv_pipe(std::string_view line) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < line.size()) {
        const std::size_t colon = line.find(':', i);
        if (colon == std::string_view::npos) break;
        const std::string_view key = line.substr(i, colon - i);
        std::size_t j = colon + 1;
        std::size_t pipe = line.find('|', j);
        if (pipe == std::string_view::npos) pipe = line.size();
        const std::string_view val = line.substr(j, pipe - j);
        out.emplace_back(std::string{key}, std::string{val});
        i = (pipe == line.size()) ? line.size() : pipe + 1;
    }
    return out;
}

/// Parse a single JSON-style keyword list `["a","b","c"]`. Tolerant: only
/// extracts bareword tokens between quotes. No escape handling — keywords
/// are expected to be short ASCII labels per the spec's examples.
[[nodiscard]] std::vector<std::string>
parse_keyword_list(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t q1 = s.find('"', i);
        if (q1 == std::string_view::npos) break;
        const std::size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string_view::npos) break;
        out.emplace_back(s.substr(q1 + 1, q2 - q1 - 1));
        i = q2 + 1;
    }
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
//  AttfHeader::get_setting
// -----------------------------------------------------------------------------
std::string AttfHeader::get_setting(std::string_view key) const {
    for (const auto& [k, v] : settings) {
        if (k == key) return v;
    }
    return {};
}

// -----------------------------------------------------------------------------
//  AttfReader::open
// -----------------------------------------------------------------------------
AttfResult<AttfReader> AttfReader::open(MemoryView buffer) noexcept {
    if (buffer.empty()) {
        return std::unexpected(AttfError::InvalidArgument);
    }

    auto bytes = buffer.bytes();

    // 1) Magic header: \x01ATTF_MEM:<version>\n
    if (bytes.size() < 2 || bytes[0] != kSoh) {
        return std::unexpected(AttfError::InvalidMagicHeader);
    }
    std::size_t cursor = 1;
    std::string_view header_line = read_line(bytes, cursor);
    constexpr std::string_view kPrefix = "ATTF_MEM:";
    if (header_line.size() < kPrefix.size() ||
        header_line.substr(0, kPrefix.size()) != kPrefix) {
        return std::unexpected(AttfError::InvalidVersion);
    }
    AttfHeader hdr;
    hdr.version = std::string{header_line.substr(kPrefix.size())};
    if (hdr.version.empty()) {
        return std::unexpected(AttfError::InvalidVersion);
    }

    AttfReader r;
    r.buffer_ = std::move(buffer);

    // 2) Settings lines (\x04-prefixed). Zero or more.
    while (cursor < bytes.size() && bytes[cursor] == kEot) {
        ++cursor;  // consume the EOT byte
        std::string_view line = read_line(bytes, cursor);
        // Multiple \x04 lines can stack; merge into the existing list.
        auto more = parse_kv_pipe(line);
        for (auto& kv : more) hdr.settings.push_back(std::move(kv));
    }

    // 3) Index rows (\x03-prefixed). Parse 400: and 401: rows until we hit \x02.
    std::vector<AttfPageEntry>     dir;
    std::vector<AttfSemanticEntry> sem;
    while (cursor < bytes.size() && bytes[cursor] != kStx) {
        if (bytes[cursor] != kEtx) {
            // Stray byte between rows: skip to next \x03.
            const std::size_t next = find_byte(bytes, kEtx, cursor + 1);
            if (next == std::string::npos) break;
            cursor = next;
            continue;
        }
        ++cursor;  // consume ETX
        std::string_view row = read_line(bytes, cursor);

        if (row.starts_with("400:")) {
            // 400:<id>,<type>,<addr>,<priority>
            std::string_view body = row.substr(4);
            const std::size_t c1 = body.find(',');
            if (c1 == std::string_view::npos) return std::unexpected(AttfError::InvalidIndexRow);
            const std::size_t c2 = body.find(',', c1 + 1);
            if (c2 == std::string_view::npos) return std::unexpected(AttfError::InvalidIndexRow);
            const std::size_t c3 = body.find(',', c2 + 1);
            if (c3 == std::string_view::npos) return std::unexpected(AttfError::InvalidIndexRow);

            AttfPageEntry e{};
            // id
            {
                auto [p, ec] = std::from_chars(body.data(), body.data() + c1, e.page_id);
                if (ec != std::errc{}) return std::unexpected(AttfError::InvalidIndexRow);
                (void)p;
            }
            e.type        = std::string{body.substr(c1 + 1, c2 - c1 - 1)};
            e.hex_address = std::string{body.substr(c2 + 1, c3 - c2 - 1)};
            // priority
            {
                const std::string_view prio_str = body.substr(c3 + 1);
                auto [p, ec] = std::from_chars(prio_str.data(),
                                                prio_str.data() + prio_str.size(),
                                                e.priority);
                if (ec != std::errc{}) return std::unexpected(AttfError::InvalidIndexRow);
                (void)p;
            }
            dir.push_back(std::move(e));
        } else if (row.starts_with("401:")) {
            // 401:<addr>,["kw1","kw2"]
            std::string_view body = row.substr(4);
            const std::size_t c1 = body.find(',');
            if (c1 == std::string_view::npos) return std::unexpected(AttfError::InvalidIndexRow);
            AttfSemanticEntry se{};
            se.hex_address = std::string{body.substr(0, c1)};
            se.keywords    = parse_keyword_list(body.substr(c1 + 1));
            sem.push_back(std::move(se));
        } else {
            // Unknown index tag — tolerate silently.
        }
    }

    // 4) Start of data stream: \x02\n
    if (cursor >= bytes.size() || bytes[cursor] != kStx) {
        return std::unexpected(AttfError::MissingDataStream);
    }
    ++cursor;  // consume STX
    if (cursor < bytes.size() && bytes[cursor] == '\n') ++cursor;

    // 5) Data pages: \x03P<id>:<addr>\n<content>\n … \x03 (terminator)
    //    Each page's content runs from just after the header newline up to
    //    (but not including) the next \x03.
    std::unordered_map<std::string, std::string_view> addr_to_content;
    std::unordered_map<std::uint32_t, std::string>     id_to_addr;

    while (cursor < bytes.size()) {
        if (bytes[cursor] != kEtx) {
            // Stray bytes outside any page block — skip to next ETX or EOF.
            const std::size_t next = find_byte(bytes, kEtx, cursor + 1);
            if (next == std::string::npos) break;
            cursor = next;
            continue;
        }
        ++cursor;  // consume ETX
        // Header line: P<id>:<addr>\n
        std::string_view page_hdr = read_line(bytes, cursor);
        if (page_hdr.empty()) {
            // Trailing terminator "\x03\n" — done.
            break;
        }
        if (page_hdr[0] != 'P') {
            // Not a page row; skip silently.
            continue;
        }
        // Parse P<id>:<addr>
        const std::size_t colon = page_hdr.find(':');
        if (colon == std::string_view::npos) {
            return std::unexpected(AttfError::InvalidPageHeader);
        }
        const std::string_view id_str  = page_hdr.substr(1, colon - 1);
        const std::string_view addr_str = page_hdr.substr(colon + 1);

        std::uint32_t pid = 0;
        {
            auto [p, ec] = std::from_chars(id_str.data(),
                                            id_str.data() + id_str.size(), pid);
            if (ec != std::errc{}) return std::unexpected(AttfError::InvalidPageHeader);
            (void)p;
        }

        // Page content: from current cursor to the next unescaped \x03.
        // ATTF v3.0 introduces an escape sequence:
        //   \x1b\x03 -> literal ETX byte
        //   \x1b\x1b -> literal ESC byte
        //
        // Zero-copy fast-path: if the page contains no escape bytes, we
        // return a view directly into the underlying buffer (avoiding a copy).
        // We detect this by scanning byte-by-byte; if we encounter kEsc, we
        // fall back to the slow path. We must skip the byte AFTER kEsc so we
        // don't mistake an escaped \x03 (i.e. \x1b\x03) for a real delimiter.
        bool needs_unescape = false;
        std::size_t scan = cursor;
        while (scan < bytes.size()) {
            const std::uint8_t b = bytes[scan];
            if (b == kEtx) break;
            if (b == kEsc) {
                needs_unescape = true;
                break;
            }
            ++scan;
        }
        // Find the page's true end (next raw \x03 that is NOT preceded by \x1b).
        // If needs_unescape is false, `scan` already points at the end.
        std::size_t end = scan;
        if (needs_unescape) {
            // Slow walk: skip \x1b + next, but stop at any standalone \x03.
            while (end < bytes.size()) {
                const std::uint8_t b = bytes[end];
                if (b == kEtx) break;
                if (b == kEsc) {
                    ++end;  // consume the escape byte
                    if (end < bytes.size()) ++end;  // consume the escaped byte
                } else {
                    ++end;
                }
            }
        }

        std::string_view content;
        if (!needs_unescape) {
            // Zero-copy fast-path.
            content = as_sv(bytes).substr(cursor, end - cursor);
        } else {
            // Slow path: scan with escape decoding.
            std::string content_buf;
            content_buf.reserve(end - cursor);
            bool escaped = false;
            for (std::size_t i = cursor; i < end; ++i) {
                const std::uint8_t b = bytes[i];
                if (escaped) {
                    if (b == kEtx)      content_buf.push_back(static_cast<char>(kEtx));
                    else if (b == kEsc) content_buf.push_back(static_cast<char>(kEsc));
                    else {
                        content_buf.push_back(static_cast<char>(kEsc));
                        content_buf.push_back(static_cast<char>(b));
                    }
                    escaped = false;
                } else if (b == kEsc) {
                    escaped = true;
                } else {
                    content_buf.push_back(static_cast<char>(b));
                }
            }
            r.escaped_pages_.push_back(std::move(content_buf));
            content = r.escaped_pages_.back();
        }
        // Strip one trailing \n if present.
        if (!content.empty() && content.back() == '\n') {
            content.remove_suffix(1);
        }
        cursor = end;

        const std::string addr_key{addr_str};
        addr_to_content[addr_key] = content;
        id_to_addr[pid] = addr_key;
    }

    r.header_           = std::move(hdr);
    r.directory_        = std::move(dir);
    r.semantic_index_   = std::move(sem);
    r.address_to_content_ = std::move(addr_to_content);
    r.id_to_address_    = std::move(id_to_addr);
    return r;
}

AttfResult<AttfReader>
AttfReader::open_file(const std::filesystem::path& path) noexcept {
    auto mv = MemoryView::from_file(path);
    if (!mv) return std::unexpected(AttfError::IoError);
    return open(std::move(*mv));
}

// -----------------------------------------------------------------------------
//  Lookups
// -----------------------------------------------------------------------------
AttfResult<std::string_view>
AttfReader::get_page(std::string_view hex_address) const noexcept {
    const auto it = address_to_content_.find(std::string{hex_address});
    if (it == address_to_content_.end()) {
        return std::unexpected(AttfError::PageNotFound);
    }
    return it->second;
}

AttfResult<std::string_view>
AttfReader::get_page_by_id(std::uint32_t page_id) const noexcept {
    const auto it = id_to_address_.find(page_id);
    if (it == id_to_address_.end()) {
        return std::unexpected(AttfError::PageNotFound);
    }
    return get_page(it->second);
}

std::vector<std::string> AttfReader::page_addresses() const {
    std::vector<std::string> out;
    out.reserve(directory_.size());
    for (const auto& e : directory_) out.push_back(e.hex_address);
    return out;
}

} // namespace tts
