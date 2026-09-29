// =============================================================================
//  include/tts/ffi.h
//  Phase 5 — C ABI for Python (ctypes/cffi) and Node.js (N-API) bindings.
//
//  All functions are extern "C". Error codes are plain ints matching the
//  values returned by tts::to_int(TtfError) / tts::to_int(AttfError).
//  0 == Ok.
//
//  Lifetime rules:
//    - Handles (tts_ttf_reader_t / tts_attf_reader_t) own their MemoryView.
//    - All string_view-returning accessors copy into a caller-provided buffer
//      AND return the required length: if *out_len < required, callers must
//      resize and retry. (FFI cannot safely return raw string_views across
//      language boundaries.)
//    - tts_last_error() returns a thread-local static description of the
//      most recent failure.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
//  Opaque handles
// -----------------------------------------------------------------------------
typedef struct tts_ttf_reader*  tts_ttf_reader_t;
typedef struct tts_attf_reader* tts_attf_reader_t;

// -----------------------------------------------------------------------------
//  Version
// -----------------------------------------------------------------------------
const char* tts_version(void);
uint32_t    tts_version_packed(void);

// -----------------------------------------------------------------------------
//  Last-error string (thread-local).
// -----------------------------------------------------------------------------
const char* tts_last_error(void);

// -----------------------------------------------------------------------------
//  TTF — open / introspect / close
// -----------------------------------------------------------------------------
//  On success returns a handle and sets *out to it. On failure returns the
//  non-zero error code (matching tts::TtfError) and sets the last-error
//  string.
int tts_ttf_open(const char* path, tts_ttf_reader_t* out);
int tts_ttf_close(tts_ttf_reader_t reader);

//  Metadata accessors — return TtfError code; on success, write to *out.
int tts_ttf_section_count(tts_ttf_reader_t reader, size_t* out);
int tts_ttf_section_id_at(tts_ttf_reader_t reader, size_t index, uint32_t* out);

//  Fetch section content.
//  Caller passes a writable buffer of capacity `cap`. On success:
//    - if cap >= required_len: content is copied, *out_len = required_len, return 0
//    - else: *out_len = required_len, return -2 (buffer-too-small; caller retries)
int tts_ttf_get_section(tts_ttf_reader_t reader,
                        uint32_t section_id,
                        char* buf, size_t cap, size_t* out_len);

//  Validate every section's hash. Returns 0 if all pass, or the first
//  failing section id (as a positive int) cast to TtfError code on failure.
//  On success, *out_first_bad_section = 0. On hash mismatch, returns
//  tts::to_int(TtfError::HashMismatch) and *out_first_bad_section is set.
int tts_ttf_validate_all(tts_ttf_reader_t reader, uint32_t* out_first_bad_section);

// -----------------------------------------------------------------------------
//  ATTF — open / introspect / close
// -----------------------------------------------------------------------------
int tts_attf_open(const char* path, tts_attf_reader_t* out);
int tts_attf_close(tts_attf_reader_t reader);

int tts_attf_directory_count(tts_attf_reader_t reader, size_t* out);

//  Fetch page content by hex address (e.g. "0x00A1").
int tts_attf_get_page(tts_attf_reader_t reader,
                      const char* hex_address,
                      char* buf, size_t cap, size_t* out_len);

//  Fetch page content by numeric page_id.
int tts_attf_get_page_by_id(tts_attf_reader_t reader,
                            uint32_t page_id,
                            char* buf, size_t cap, size_t* out_len);

// -----------------------------------------------------------------------------
//  v3.0 — Semantic search + agent memory
// -----------------------------------------------------------------------------
//  Search an ATTF file's embeddings (Tag 402) for nearest neighbours to
//  `query_vector` (dim floats). Returns matches via callback or pre-allocated
//  buffer. For simplicity we return one match at a time via repeated calls.
typedef struct tts_attf_search_iter* tts_attf_search_iter_t;

int tts_attf_search_open(tts_attf_reader_t reader,
                         const float* query_vector,
                         uint32_t dim,
                         uint32_t top_k,
                         tts_attf_search_iter_t* out);
int tts_attf_search_next(tts_attf_search_iter_t it,
                         char* addr_buf, size_t addr_cap, size_t* addr_len,
                         float* out_score);
int tts_attf_search_close(tts_attf_search_iter_t it);

// -----------------------------------------------------------------------------
//  v3.0 — Compression / Encryption
// -----------------------------------------------------------------------------
//  Returns 1 if zstd is built in, 0 otherwise.
int tts_has_zstd(void);

// Returns 1 if AES-256-GCM is available (always 1 in this build).
int tts_has_aes_gcm(void);

// -----------------------------------------------------------------------------
//  Error stringification
// -----------------------------------------------------------------------------
const char* tts_ttf_error_str(int code);
const char* tts_attf_error_str(int code);

#ifdef __cplusplus
} // extern "C"
#endif
