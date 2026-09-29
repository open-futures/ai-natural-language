// =============================================================================
//  src/wal.cpp — M3
// =============================================================================
#include "tts/wal.hpp"

#include "tts/sha256.hpp"
#include "tts/utf8_utils.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

namespace tts {

namespace {

constexpr std::string_view kWalMagic = "[WALv1]\n";

[[nodiscard]] std::string format_op(const WalOp& op) {
    std::ostringstream ss;
    switch (op.kind) {
        case WalOp::Kind::Put:
            ss << "PUT " << op.section_id << ' '
               << op.offset_chars << ' '
               << op.length_chars << ' '
               << op.sha256_hex << '\n';
            break;
        case WalOp::Kind::Del:
            ss << "DEL " << op.section_id << '\n';
            break;
        case WalOp::Kind::Commit:
            ss << "COMMIT " << op.tx_id << '\n';
            break;
    }
    return ss.str();
}

} // namespace

TtfResult<TtfWal> TtfWal::open(const std::filesystem::path& base_path) {
    TtfWal w;
    w.path_ = base_path.string() + ".wal";
    // Create the file with magic if it doesn't exist.
    std::error_code ec;
    if (!std::filesystem::exists(w.path_, ec)) {
        std::ofstream f(w.path_, std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(TtfError::IoError);
        f.write(kWalMagic.data(), static_cast<std::streamsize>(kWalMagic.size()));
        if (!f) return std::unexpected(TtfError::IoError);
    }
    return w;  // implicit conversion via expected's converting ctor
}

TtfResult<std::uint64_t>
TtfWal::append_put(std::uint32_t section_id,
                   std::uint64_t offset_chars, std::uint64_t length_chars,
                   std::string sha256_hex) {
    std::lock_guard lock(*mutex_);
    std::ofstream f(path_, std::ios::binary | std::ios::app);
    if (!f) return std::unexpected(TtfError::IoError);
    WalOp op{};
    op.kind         = WalOp::Kind::Put;
    op.section_id   = section_id;
    op.offset_chars = offset_chars;
    op.length_chars = length_chars;
    op.sha256_hex   = std::move(sha256_hex);
    const auto line = format_op(op);
    const auto pos  = static_cast<std::uint64_t>(f.tellp());
    f.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (!f) return std::unexpected(TtfError::IoError);
    f.flush();
    return pos;
}

TtfResult<std::uint64_t>
TtfWal::append_del(std::uint32_t section_id) {
    std::lock_guard lock(*mutex_);
    std::ofstream f(path_, std::ios::binary | std::ios::app);
    if (!f) return std::unexpected(TtfError::IoError);
    WalOp op{};
    op.kind       = WalOp::Kind::Del;
    op.section_id = section_id;
    const auto line = format_op(op);
    const auto pos  = static_cast<std::uint64_t>(f.tellp());
    f.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (!f) return std::unexpected(TtfError::IoError);
    f.flush();
    return pos;
}

TtfStatus TtfWal::commit(std::uint64_t tx_id) {
    std::lock_guard lock(*mutex_);
    std::ofstream f(path_, std::ios::binary | std::ios::app);
    if (!f) return std::unexpected(TtfError::IoError);
    WalOp op{};
    op.kind  = WalOp::Kind::Commit;
    op.tx_id = tx_id;
    const auto line = format_op(op);
    f.write(line.data(), static_cast<std::streamsize>(line.size()));
    if (!f) return std::unexpected(TtfError::IoError);
    f.flush();
    return {};
}

TtfStatus TtfWal::truncate() {
    std::lock_guard lock(*mutex_);
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f) return std::unexpected(TtfError::IoError);
    f.write(kWalMagic.data(), static_cast<std::streamsize>(kWalMagic.size()));
    if (!f) return std::unexpected(TtfError::IoError);
    return {};
}

std::vector<WalOp> TtfWal::read_committed() const {
    std::lock_guard lock(*mutex_);
    std::vector<WalOp> all;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return all;
    std::string line;
    // Skip magic
    std::getline(in, line);
    std::vector<WalOp> buffer;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "PUT") {
            WalOp op{};
            op.kind = WalOp::Kind::Put;
            ss >> op.section_id >> op.offset_chars >> op.length_chars >> op.sha256_hex;
            buffer.push_back(std::move(op));
        } else if (tag == "DEL") {
            WalOp op{};
            op.kind = WalOp::Kind::Del;
            ss >> op.section_id;
            buffer.push_back(std::move(op));
        } else if (tag == "COMMIT") {
            WalOp op{};
            op.kind = WalOp::Kind::Commit;
            ss >> op.tx_id;
            buffer.push_back(std::move(op));
            // Promote buffer into `all` up to and including this commit.
            for (auto& b : buffer) all.push_back(std::move(b));
            buffer.clear();
        }
    }
    // Uncommitted ops in `buffer` are discarded.
    return all;
}

// -----------------------------------------------------------------------------
//  atomic_replace_section — full pipeline
// -----------------------------------------------------------------------------
TtfStatus
atomic_replace_section(const std::filesystem::path& ttf_path,
                       const TtfReader& current_state,
                       std::uint32_t section_id,
                       std::string_view new_content) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // Open WAL
    auto wal_r = TtfWal::open(ttf_path);
    if (!wal_r) return std::unexpected(wal_r.error());

    // Build a new writer with all existing sections, replacing the target.
    tts::TtfWriter w;
    w.set_tfd_id(current_state.metadata().tfd_id);
    w.set_next_tfd(current_state.metadata().next_tfd);
    w.set_encoding(current_state.metadata().encoding);

    bool found = false;
    for (const auto& s : current_state.metadata().sections) {
        if (s.section_id == section_id) {
            w.add_section(section_id, std::string{new_content});
            found = true;
            // Log to WAL
            const auto off = utf8::count_chars(current_state.data_stream());
            const auto len = utf8::count_chars(new_content);
            const auto sha = crypto::sha256_hex(new_content);
            (void)wal_r->append_put(section_id, off, len, sha);
        } else {
            auto sec = current_state.get_section(s.section_id);
            if (!sec) return std::unexpected(sec.error());
            w.add_section(s.section_id, std::string{*sec});
        }
    }
    if (!found) {
        // Append a brand-new section.
        w.add_section(section_id, std::string{new_content});
        const auto sha = crypto::sha256_hex(new_content);
        const auto len = utf8::count_chars(new_content);
        (void)wal_r->append_put(section_id, 0, len, sha);
    }

    // Commit (we use a monotonic-ish timestamp as tx_id)
    const auto tx_id = static_cast<std::uint64_t>(std::time(nullptr));
    if (auto st = wal_r->commit(tx_id); !st) return std::unexpected(st.error());

    // Atomically swap the new TTF into place.
    if (auto st = w.write_to_file(ttf_path); !st) return std::unexpected(st.error());

    // Truncate the WAL — new readers will pick up the new TTF directly.
    if (auto st = wal_r->truncate(); !st) return std::unexpected(st.error());
    return {};
}

} // namespace tts
