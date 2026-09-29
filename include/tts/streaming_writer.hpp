// =============================================================================
//  include/tts/streaming_writer.hpp
//  Phase M0 — Streaming TTF writer.
//
//  Builds a TTF document directly into an ostream without materialising the
//  full document in memory. Required for very large documents (>100 MB).
//
//  Constraint: the directory must be emitted FIRST (before the data stream),
//  so the streaming writer must buffer section metadata (offset/length/hash)
//  while streaming the content. This means we still need to *compute* the
//  offset/length/hash of each section before emitting the directory — but
//  the section CONTENT is streamed directly to the ostream.
//
//  Two-pass algorithm:
//    Pass 1: walk sections, compute (offset_chars, length_chars, sha256).
//            Emit directory. Then emit [DATA_STREAM_START]\n.
//    Pass 2: re-walk sections, emit content.
//
//  The section content is therefore consumed twice. If the caller's source
//  is a file path, the writer re-reads it; if it's a callable, the callable
//  is invoked twice. We expose both forms.
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace tts {

class StreamingTtfWriter {
public:
    StreamingTtfWriter();
    ~StreamingTtfWriter();
    StreamingTtfWriter(StreamingTtfWriter&&) noexcept;
    StreamingTtfWriter& operator=(StreamingTtfWriter&&) noexcept;

    StreamingTtfWriter& set_tfd_id(std::uint32_t id) noexcept       { tfd_id_ = id; return *this; }
    StreamingTtfWriter& set_next_tfd(std::uint32_t id) noexcept     { next_tfd_ = id; return *this; }
    StreamingTtfWriter& set_encoding(std::string enc) noexcept      { encoding_ = std::move(enc); return *this; }

    /// Declare a section. The content is NOT copied — instead, on each
    /// emission pass, `source` is invoked and must write the section's
    /// UTF-8 bytes to the provided ostream.
    StreamingTtfWriter& add_section_source(
        std::uint32_t section_id,
        std::function<void(std::ostream&)> source);

    /// Convenience: declare a section whose content is a static string.
    /// (Internally captured by value to keep the lambda alive.)
    StreamingTtfWriter& add_section(std::uint32_t section_id, std::string content);

    /// Declare a section backed by a file. The file is read on each pass.
    StreamingTtfWriter& add_section_file(std::uint32_t section_id,
                                          std::filesystem::path file);

    /// Emit the document to the given ostream. Returns the total byte count.
    [[nodiscard]] TtfResult<std::uint64_t> write_to(std::ostream& out) const;

    /// Convenience: emit to a file.
    [[nodiscard]] TtfStatus write_to_file(const std::filesystem::path& path) const;

private:
    struct PendingSource {
        std::uint32_t id;
        std::function<void(std::ostream&)> source;
    };
    std::uint32_t                  tfd_id_   = 1;
    std::uint32_t                  next_tfd_ = 0;
    std::string                    encoding_ = "UTF-8";
    std::vector<PendingSource>     sources_;
};

} // namespace tts
