// =============================================================================
//  src/ffi.cpp — Phase 5 C-ABI implementation
// =============================================================================
#include "tts/ffi.h"

#include "tts/attf_reader.hpp"
#include "tts/compression.hpp"
#include "tts/semantic_search.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/types.hpp"
#include "tts/version.hpp"

#include <atomic>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Per-thread last error message — used by FFI consumers (Python ctypes, etc.)
// that can't propagate std::expected across the boundary.
thread_local std::string g_last_error;

void set_last_error(std::string_view msg) {
    g_last_error.assign(msg);
}

// A small RAII guard wrapper so the C handle is just a pointer to the C++ object.
struct TtfReaderHandle {
    tts::TtfReader reader;
    explicit TtfReaderHandle(tts::TtfReader r) : reader(std::move(r)) {}
};

struct AttfReaderHandle {
    tts::AttfReader reader;
    explicit AttfReaderHandle(tts::AttfReader r) : reader(std::move(r)) {}
};

} // namespace

extern "C" {

// -----------------------------------------------------------------------------
//  Version
// -----------------------------------------------------------------------------
const char* tts_version(void) {
    // tts::version() returns a string_view into a static constant.
    static const std::string v{std::string{tts::version()}};
    return v.c_str();
}

uint32_t tts_version_packed(void) {
    return tts::version_packed();
}

// -----------------------------------------------------------------------------
//  Last error
// -----------------------------------------------------------------------------
const char* tts_last_error(void) {
    return g_last_error.c_str();
}

// -----------------------------------------------------------------------------
//  TTF
// -----------------------------------------------------------------------------
int tts_ttf_open(const char* path, tts_ttf_reader_t* out) {
    if (!path || !out) { set_last_error("null argument"); return static_cast<int>(tts::TtfError::InvalidArgument); }
    auto r = tts::TtfReader::open_file(path);
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    auto* h = new (std::nothrow) TtfReaderHandle(std::move(*r));
    if (!h) { set_last_error("out of memory"); return static_cast<int>(tts::TtfError::IoError); }
    *out = reinterpret_cast<tts_ttf_reader_t>(h);
    return 0;
}

int tts_ttf_close(tts_ttf_reader_t reader) {
    if (!reader) return 0;
    delete reinterpret_cast<TtfReaderHandle*>(reader);
    return 0;
}

int tts_ttf_section_count(tts_ttf_reader_t reader, size_t* out) {
    if (!reader || !out) return static_cast<int>(tts::TtfError::InvalidArgument);
    const auto* h = reinterpret_cast<TtfReaderHandle*>(reader);
    *out = h->reader.metadata().sections.size();
    return 0;
}

int tts_ttf_section_id_at(tts_ttf_reader_t reader, size_t index, uint32_t* out) {
    if (!reader || !out) return static_cast<int>(tts::TtfError::InvalidArgument);
    const auto* h = reinterpret_cast<TtfReaderHandle*>(reader);
    if (index >= h->reader.metadata().sections.size()) {
        return static_cast<int>(tts::TtfError::SectionNotFound);
    }
    *out = h->reader.metadata().sections[index].section_id;
    return 0;
}

int tts_ttf_get_section(tts_ttf_reader_t reader,
                        uint32_t section_id,
                        char* buf, size_t cap, size_t* out_len) {
    if (!reader || !out_len) return static_cast<int>(tts::TtfError::InvalidArgument);
    const auto* h = reinterpret_cast<TtfReaderHandle*>(reader);
    auto r = h->reader.get_section(section_id);
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    const std::size_t need = r->size();
    *out_len = need;
    if (buf && cap >= need) {
        std::memcpy(buf, r->data(), need);
        return 0;
    }
    return -2;  // buffer too small; *out_len has the required size
}

int tts_ttf_validate_all(tts_ttf_reader_t reader, uint32_t* out_first_bad_section) {
    if (!reader) return static_cast<int>(tts::TtfError::InvalidArgument);
    const auto* h = reinterpret_cast<TtfReaderHandle*>(reader);
    auto r = h->reader.validate_all();
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    if (out_first_bad_section) *out_first_bad_section = *r;
    return 0;
}

// -----------------------------------------------------------------------------
//  ATTF
// -----------------------------------------------------------------------------
int tts_attf_open(const char* path, tts_attf_reader_t* out) {
    if (!path || !out) { set_last_error("null argument"); return static_cast<int>(tts::AttfError::InvalidArgument); }
    auto r = tts::AttfReader::open_file(path);
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    auto* h = new (std::nothrow) AttfReaderHandle(std::move(*r));
    if (!h) { set_last_error("out of memory"); return static_cast<int>(tts::AttfError::IoError); }
    *out = reinterpret_cast<tts_attf_reader_t>(h);
    return 0;
}

int tts_attf_close(tts_attf_reader_t reader) {
    if (!reader) return 0;
    delete reinterpret_cast<AttfReaderHandle*>(reader);
    return 0;
}

int tts_attf_directory_count(tts_attf_reader_t reader, size_t* out) {
    if (!reader || !out) return static_cast<int>(tts::AttfError::InvalidArgument);
    const auto* h = reinterpret_cast<AttfReaderHandle*>(reader);
    *out = h->reader.directory().size();
    return 0;
}

int tts_attf_get_page(tts_attf_reader_t reader,
                      const char* hex_address,
                      char* buf, size_t cap, size_t* out_len) {
    if (!reader || !hex_address || !out_len) return static_cast<int>(tts::AttfError::InvalidArgument);
    const auto* h = reinterpret_cast<AttfReaderHandle*>(reader);
    auto r = h->reader.get_page(std::string_view{hex_address});
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    const std::size_t need = r->size();
    *out_len = need;
    if (buf && cap >= need) {
        std::memcpy(buf, r->data(), need);
        return 0;
    }
    return -2;
}

int tts_attf_get_page_by_id(tts_attf_reader_t reader,
                            uint32_t page_id,
                            char* buf, size_t cap, size_t* out_len) {
    if (!reader || !out_len) return static_cast<int>(tts::AttfError::InvalidArgument);
    const auto* h = reinterpret_cast<AttfReaderHandle*>(reader);
    auto r = h->reader.get_page_by_id(page_id);
    if (!r) {
        set_last_error(tts::to_string(r.error()));
        return static_cast<int>(r.error());
    }
    const std::size_t need = r->size();
    *out_len = need;
    if (buf && cap >= need) {
        std::memcpy(buf, r->data(), need);
        return 0;
    }
    return -2;
}

const char* tts_ttf_error_str(int code) {
    return tts::to_string(static_cast<tts::TtfError>(code));
}

const char* tts_attf_error_str(int code) {
    return tts::to_string(static_cast<tts::AttfError>(code));
}

// -----------------------------------------------------------------------------
//  v3.0 — Semantic search iterator
// -----------------------------------------------------------------------------
struct TtsAttfSearchIter {
    std::vector<tts::semantic::SearchResult> results;
    std::size_t cursor = 0;
};

int tts_attf_search_open(tts_attf_reader_t reader,
                         const float* query_vector,
                         uint32_t dim,
                         uint32_t top_k,
                         tts_attf_search_iter_t* out) {
    if (!reader || !query_vector || !out) return static_cast<int>(tts::AttfError::InvalidArgument);
    const auto* h = reinterpret_cast<AttfReaderHandle*>(reader);
    auto idx = tts::semantic::parse_index(h->reader);
    if (idx.entries.empty()) {
        *out = nullptr;
        return 0;
    }
    tts::semantic::SearchOpts opts;
    opts.top_k = top_k;
    auto results = tts::semantic::search_flat(idx,
        std::span<const float>{query_vector, dim},
        h->reader, opts);
    auto* it = new (std::nothrow) TtsAttfSearchIter{std::move(results), 0};
    if (!it) return static_cast<int>(tts::AttfError::IoError);
    *out = reinterpret_cast<tts_attf_search_iter_t>(it);
    return 0;
}

int tts_attf_search_next(tts_attf_search_iter_t it,
                         char* addr_buf, size_t addr_cap, size_t* addr_len,
                         float* out_score) {
    if (!it) return static_cast<int>(tts::AttfError::InvalidArgument);
    auto* p = reinterpret_cast<TtsAttfSearchIter*>(it);
    if (p->cursor >= p->results.size()) return 1;  // end of iteration
    const auto& r = p->results[p->cursor++];
    if (addr_len) *addr_len = r.hex_address.size();
    if (addr_buf && addr_cap >= r.hex_address.size()) {
        std::memcpy(addr_buf, r.hex_address.data(), r.hex_address.size());
    } else if (addr_buf) {
        return -2;  // buffer too small
    }
    if (out_score) *out_score = r.score;
    return 0;
}

int tts_attf_search_close(tts_attf_search_iter_t it) {
    if (!it) return 0;
    delete reinterpret_cast<TtsAttfSearchIter*>(it);
    return 0;
}

// -----------------------------------------------------------------------------
//  v3.0 — Feature flags
// -----------------------------------------------------------------------------
int tts_has_zstd(void) {
#if defined(TTS_USE_ZSTD) && TTS_USE_ZSTD
    return 1;
#else
    return 0;
#endif
}

int tts_has_aes_gcm(void) {
    return 1;  // always available in-tree
}

} // extern "C"
