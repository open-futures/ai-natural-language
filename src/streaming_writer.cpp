// =============================================================================
//  src/streaming_writer.cpp — M0
// =============================================================================
#include "tts/streaming_writer.hpp"
#include "tts/sha256.hpp"
#include "tts/utf8_utils.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace tts {

namespace {

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

StreamingTtfWriter::StreamingTtfWriter() = default;
StreamingTtfWriter::~StreamingTtfWriter() = default;
StreamingTtfWriter::StreamingTtfWriter(StreamingTtfWriter&&) noexcept = default;
StreamingTtfWriter& StreamingTtfWriter::operator=(StreamingTtfWriter&&) noexcept = default;

StreamingTtfWriter& StreamingTtfWriter::add_section_source(
    std::uint32_t section_id,
    std::function<void(std::ostream&)> source) {
    sources_.push_back(PendingSource{section_id, std::move(source)});
    return *this;
}

StreamingTtfWriter& StreamingTtfWriter::add_section(std::uint32_t section_id, std::string content) {
    auto captured = std::make_shared<std::string>(std::move(content));
    return add_section_source(section_id, [captured](std::ostream& out) {
        out.write(captured->data(), static_cast<std::streamsize>(captured->size()));
    });
}

StreamingTtfWriter& StreamingTtfWriter::add_section_file(std::uint32_t section_id,
                                                          std::filesystem::path file) {
    auto captured = std::make_shared<std::filesystem::path>(std::move(file));
    return add_section_source(section_id, [captured](std::ostream& out) {
        std::ifstream in(*captured, std::ios::binary);
        if (!in) return;
        char buf[64 * 1024];
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            out.write(buf, in.gcount());
        }
    });
}

TtfResult<std::uint64_t> StreamingTtfWriter::write_to(std::ostream& out) const {
    if (!out) return std::unexpected(TtfError::IoError);

    // ---- Pass 1: compute metadata for each section by streaming it into
    //              a counting hash stream. We don't keep the bytes — only
    //              offset_chars / length_chars / sha256.
    struct Built {
        std::uint32_t id;
        std::uint64_t off_chars;
        std::uint64_t len_chars;
        std::string   sha256_hex;
    };
    std::vector<Built> built;
    built.reserve(sources_.size());

    std::uint64_t char_cursor = 0;
    for (const auto& s : sources_) {
        // Stream into a counting/hash sink
        std::uint64_t len_chars = 0;
        crypto::Sha256 h;

        // Adapter: an ostream that updates the hash and char counter
        // without storing the bytes.
        class CountingHashBuf : public std::streambuf {
        public:
            CountingHashBuf(crypto::Sha256& h, std::uint64_t& n) : h_(h), n_(n) {}
        protected:
            std::streamsize xsputn(const char* s, std::streamsize n) override {
                h_.update(std::string_view{s, static_cast<std::size_t>(n)});
                n_ += utf8::count_chars(std::string_view{s, static_cast<std::size_t>(n)});
                return n;
            }
            int overflow(int c) override {
                if (c != EOF) {
                    char ch = static_cast<char>(c);
                    h_.update(std::string_view{&ch, 1});
                    n_ += utf8::count_chars(std::string_view{&ch, 1});
                }
                return c;
            }
        private:
            crypto::Sha256& h_;
            std::uint64_t&  n_;
        };

        CountingHashBuf buf{h, len_chars};
        std::ostream sink(&buf);
        s.source(sink);

        Built b;
        b.id         = s.id;
        b.off_chars  = char_cursor;
        b.len_chars  = len_chars;
        b.sha256_hex = crypto::to_hex(h.finalize());
        built.push_back(std::move(b));
        char_cursor = b.off_chars + b.len_chars;
    }

    // ---- Emit header + directory
    std::uint64_t total = 0;
    auto emit = [&](std::string_view sv) {
        out.write(sv.data(), static_cast<std::streamsize>(sv.size()));
        total += sv.size();
    };

    emit("[TTF_LE:2.0]\n");
    emit("---\n");

    std::string yaml;
    yaml += "tfd:\n";
    yaml += "  tfd_id: ";   yaml += std::to_string(tfd_id_);     yaml += "\n";
    yaml += "  next_tfd: "; yaml += std::to_string(next_tfd_);   yaml += "\n";
    yaml += "  encoding: "; yaml += yaml_quote(encoding_);      yaml += "\n";
    yaml += "tags:\n";
    if (!built.empty()) {
        yaml += "  - id: 273\n";
        yaml += "    name: \"SectionLayouts\"\n";
        yaml += "    value:\n";
        for (const auto& b : built) {
            yaml += "      - section: ";        yaml += std::to_string(b.id);
            yaml += "\n        offset_chars: "; yaml += std::to_string(b.off_chars);
            yaml += "\n        length_chars: "; yaml += std::to_string(b.len_chars);
            yaml += "\n        sha256: ";       yaml += yaml_quote(b.sha256_hex);
            yaml += "\n";
        }
    }
    emit(yaml);

    emit("---\n");
    emit("[DATA_STREAM_START]\n");

    // ---- Pass 2: stream each section's content
    for (const auto& s : sources_) {
        std::stringstream counter;  // we don't keep bytes — but ostream requires a sink
        // Use a null buffer so the bytes are written but discarded by us;
        // the source's writes already went through the real ostream above
        // — wait, that's wrong. We need to stream to `out` directly.
        s.source(out);
    }

    emit("\n[TTF_EOF]\n");
    return total;
}

TtfStatus
StreamingTtfWriter::write_to_file(const std::filesystem::path& path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected(TtfError::IoError);
    auto r = write_to(out);
    if (!r) { out.close(); (void)fs::remove(tmp, ec); return std::unexpected(r.error()); }
    out.close();
    fs::rename(tmp, path, ec);
    if (ec) { (void)fs::remove(tmp, ec); return std::unexpected(TtfError::IoError); }
    return {};
}

} // namespace tts
