// =============================================================================
//  src/agent_memory.cpp — M5
// =============================================================================
#include "tts/agent_memory.hpp"

#include "tts/sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tts::agent {

namespace {

[[nodiscard]] std::string make_hex_address(std::uint32_t page_id,
                                          std::string_view content) {
    // 0x + 4 hex digits (page_id) + 4 hex digits (content hash prefix)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", page_id & 0xFFFF);
    std::string out{buf};
    // Append 4 hex digits from a SHA-256 of the content.
    auto digest = crypto::sha256(content);
    std::snprintf(buf, sizeof(buf), "%02X%02X", digest[0], digest[1]);
    out += buf;
    return out;
}

} // namespace

TtfResult<Memory> Memory::open(std::filesystem::path path) {
    Memory m;
    m.path_ = path;
    if (!std::filesystem::exists(path)) {
        // Create an empty file with just the header.
        AttfWriter w;
        w.set_version("3.0");
        w.add_setting("mem_zone", "active");
        if (auto st = w.write_to_file(path); !st) return std::unexpected(attf_to_ttf(st.error()));
    }
    auto r = AttfReader::open_file(path);
    if (!r) return std::unexpected(attf_to_ttf(r.error()));
    m.reader_ = std::move(*r);
    m.directory_snapshot_ = m.reader_.directory();
    return m;
}

TtfResult<PageRef>
Memory::remember(std::string_view content, const RememberOpts& opts) {
    // Generate next page_id (max existing + 1).
    std::uint32_t next_id = 1;
    for (const auto& e : directory_snapshot_) {
        if (e.page_id >= next_id) next_id = e.page_id + 1;
    }
    const std::string addr = make_hex_address(next_id, content);

    // Rebuild the entire file with the new page appended.
    AttfWriter w;
    w.set_version(reader_.header().version);
    for (const auto& kv : reader_.header().settings) {
        w.add_setting(kv.first, kv.second);
    }
    // Re-emit existing pages
    for (const auto& e : reader_.directory()) {
        auto page = reader_.get_page(e.hex_address);
        if (!page) continue;
        // Find keywords for this page from the semantic index
        std::vector<std::string> kws;
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address == e.hex_address) { kws = s.keywords; break; }
        }
        w.add_page(e.page_id, e.type, e.hex_address, e.priority,
                   std::string{*page}, std::move(kws));
    }
    // Append the new page
    std::string content_to_store{content};
    if (opts.embedder) {
        auto vec_r = opts.embedder->embed(content);
        if (vec_r) {
            // Prefix content with a v3.0 marker + CSV float vector.
            // Construct with explicit length because the marker starts with
            // a NUL byte which would otherwise terminate a C string literal.
            std::string prefix = std::string{'\0'} + "EMB:";
            for (std::size_t i = 0; i < vec_r->size(); ++i) {
                if (i) prefix += ',';
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6g", (*vec_r)[i]);
                prefix += buf;
            }
            prefix += '\n';
            content_to_store = prefix + std::string{content};
        }
    }
    w.add_page(next_id, opts.type, addr, opts.priority,
               content_to_store, opts.keywords);
    if (auto st = w.write_to_file(path_); !st) return std::unexpected(attf_to_ttf(st.error()));

    // Reopen
    auto r = AttfReader::open_file(path_);
    if (!r) return std::unexpected(attf_to_ttf(r.error()));
    reader_ = std::move(*r);
    directory_snapshot_ = reader_.directory();

    PageRef ref;
    ref.page_id      = next_id;
    ref.hex_address  = addr;
    ref.type         = opts.type;
    ref.priority     = opts.priority;
    ref.keywords     = opts.keywords;
    auto page = reader_.get_page(addr);
    if (page) ref.content = *page;
    return ref;
}

TtfResult<std::vector<PageRef>>
Memory::recall(const RecallQuery& query) const {
    std::vector<PageRef> out;
    if (query.query_embedding) {
        auto idx = semantic::parse_index(reader_);
        if (!idx.entries.empty()) {
            semantic::SearchOpts sopts;
            sopts.top_k      = query.top_k;
            sopts.type_filter = query.type_filter;
            auto results = semantic::search_hybrid(
                reader_, idx, *query.query_embedding,
                query.task_description,
                /*kw=*/0.3f, /*sem=*/0.7f, sopts);
            for (const auto& r : results) {
                PageRef ref;
                ref.hex_address = r.hex_address;
                ref.content     = r.page_content;
                for (const auto& e : reader_.directory()) {
                    if (e.hex_address == r.hex_address) {
                        ref.page_id  = e.page_id;
                        ref.type     = e.type;
                        ref.priority = e.priority;
                        break;
                    }
                }
                for (const auto& s : reader_.semantic_index()) {
                    if (s.hex_address == r.hex_address) {
                        ref.keywords = s.keywords;
                        break;
                    }
                }
                out.push_back(std::move(ref));
            }
            return out;
        }
    }

    // Fallback: keyword-based recall.
    for (const auto& e : reader_.directory()) {
        if (e.priority < query.min_priority) continue;
        if (!query.type_filter.empty()) {
            bool match = false;
            for (const auto& t : query.type_filter) if (t == e.type) { match = true; break; }
            if (!match) continue;
        }
        // Keyword match: any keyword from this page's semantic entry is a
        // substring of the task_description.
        bool kw_match = query.task_description.empty();
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address != e.hex_address) continue;
            for (const auto& k : s.keywords) {
                if (query.task_description.find(k) != std::string_view::npos) {
                    kw_match = true; break;
                }
            }
        }
        if (!kw_match) continue;
        PageRef ref;
        ref.page_id  = e.page_id;
        ref.hex_address = e.hex_address;
        ref.type     = e.type;
        ref.priority = e.priority;
        auto page    = reader_.get_page(e.hex_address);
        if (page) ref.content = *page;
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address == e.hex_address) { ref.keywords = s.keywords; break; }
        }
        out.push_back(std::move(ref));
        if (out.size() >= query.top_k) break;
    }
    return out;
}

TtfResult<PageRef>
Memory::recall_by_id(std::uint32_t page_id) const {
    auto page = reader_.get_page_by_id(page_id);
    if (!page) return std::unexpected(attf_to_ttf(page.error()));
    PageRef ref;
    ref.page_id  = page_id;
    ref.content  = *page;
    for (const auto& e : reader_.directory()) {
        if (e.page_id == page_id) {
            ref.hex_address = e.hex_address;
            ref.type     = e.type;
            ref.priority = e.priority;
            break;
        }
    }
    return ref;
}

TtfResult<PageRef>
Memory::recall_by_address(std::string_view hex_address) const {
    auto page = reader_.get_page(hex_address);
    if (!page) return std::unexpected(attf_to_ttf(page.error()));
    PageRef ref;
    ref.hex_address = std::string{hex_address};
    ref.content     = *page;
    for (const auto& e : reader_.directory()) {
        if (e.hex_address == hex_address) {
            ref.page_id  = e.page_id;
            ref.type     = e.type;
            ref.priority = e.priority;
            break;
        }
    }
    return ref;
}

TtfResult<std::vector<PageRef>>
Memory::recall_recent(std::uint32_t n) const {
    std::vector<PageRef> out;
    auto dir = reader_.directory();
    std::sort(dir.begin(), dir.end(),
              [](const AttfPageEntry& a, const AttfPageEntry& b) {
                  return a.page_id > b.page_id;
              });
    for (const auto& e : dir) {
        if (out.size() >= n) break;
        PageRef ref;
        ref.page_id  = e.page_id;
        ref.hex_address = e.hex_address;
        ref.type     = e.type;
        ref.priority = e.priority;
        auto page    = reader_.get_page(e.hex_address);
        if (page) ref.content = *page;
        out.push_back(std::move(ref));
    }
    return out;
}

TtfStatus Memory::reindex(embeddings::IEmbedder& embedder) {
    // Rebuild the file with embeddings attached to every page.
    AttfWriter w;
    w.set_version(reader_.header().version);
    for (const auto& kv : reader_.header().settings) {
        w.add_setting(kv.first, kv.second);
    }
    w.add_setting("embedding_model", embedder.model_name());
    w.add_setting("embedding_dim",   std::to_string(embedder.dim()));
    w.add_setting("metric",          "cosine");

    for (const auto& e : reader_.directory()) {
        auto page = reader_.get_page(e.hex_address);
        if (!page) continue;
        std::vector<std::string> kws;
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address == e.hex_address) { kws = s.keywords; break; }
        }
        // Strip any existing embedding prefix
        std::string content{*page};
        if (content.size() >= 5 && content[0] == '\x00' && content.substr(1, 4) == "EMB:") {
            const auto nl = content.find('\n', 5);
            if (nl != std::string::npos) content = content.substr(nl + 1);
            else content.clear();
        }
        // Re-embed
        auto vec_r = embedder.embed(content);
        if (vec_r) {
            std::string prefix = std::string{'\0'} + "EMB:";
            for (std::size_t i = 0; i < vec_r->size(); ++i) {
                if (i) prefix += ',';
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6g", (*vec_r)[i]);
                prefix += buf;
            }
            prefix += '\n';
            content = prefix + content;
        }
        w.add_page(e.page_id, e.type, e.hex_address, e.priority,
                   content, std::move(kws));
    }
    if (auto st = w.write_to_file(path_); !st) return std::unexpected(attf_to_ttf(st.error()));
    auto r = AttfReader::open_file(path_);
    if (!r) return std::unexpected(attf_to_ttf(r.error()));
    reader_ = std::move(*r);
    directory_snapshot_ = reader_.directory();
    return {};
}

TtfStatus Memory::forget(std::uint32_t page_id) {
    AttfWriter w;
    w.set_version(reader_.header().version);
    for (const auto& kv : reader_.header().settings) {
        w.add_setting(kv.first, kv.second);
    }
    for (const auto& e : reader_.directory()) {
        auto page = reader_.get_page(e.hex_address);
        if (!page) continue;
        std::vector<std::string> kws;
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address == e.hex_address) { kws = s.keywords; break; }
        }
        double priority = e.priority;
        std::string type = e.type;
        if (e.page_id == page_id) {
            priority = 0.0;
            type     = "forgotten";
        }
        w.add_page(e.page_id, type, e.hex_address, priority,
                   std::string{*page}, std::move(kws));
    }
    if (auto st = w.write_to_file(path_); !st) return std::unexpected(attf_to_ttf(st.error()));
    auto r = AttfReader::open_file(path_);
    if (!r) return std::unexpected(attf_to_ttf(r.error()));
    reader_ = std::move(*r);
    directory_snapshot_ = reader_.directory();
    return {};
}

TtfStatus Memory::consolidate() {
    AttfWriter w;
    w.set_version(reader_.header().version);
    for (const auto& kv : reader_.header().settings) {
        w.add_setting(kv.first, kv.second);
    }
    for (const auto& e : reader_.directory()) {
        if (e.priority <= 0.0) continue;  // drop forgotten
        auto page = reader_.get_page(e.hex_address);
        if (!page) continue;
        std::vector<std::string> kws;
        for (const auto& s : reader_.semantic_index()) {
            if (s.hex_address == e.hex_address) { kws = s.keywords; break; }
        }
        w.add_page(e.page_id, e.type, e.hex_address, e.priority,
                   std::string{*page}, std::move(kws));
    }
    if (auto st = w.write_to_file(path_); !st) return std::unexpected(attf_to_ttf(st.error()));
    auto r = AttfReader::open_file(path_);
    if (!r) return std::unexpected(attf_to_ttf(r.error()));
    reader_ = std::move(*r);
    directory_snapshot_ = reader_.directory();
    return {};
}

} // namespace tts::agent
