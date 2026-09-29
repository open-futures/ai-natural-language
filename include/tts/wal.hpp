// =============================================================================
//  include/tts/wal.hpp
//  Phase M3 — Write-Ahead Log for atomic, crash-safe section updates.
//
//  This is MVCC-lite: a single-writer, multi-reader model.
//
//  Sidecar file format (one .wal per .ttf):
//    [WALv1]\n
//    <op1>\n
//    <op2>\n
//    ...
//
//  Each op is one line of form:
//    PUT <section_id> <offset_chars> <length_chars> <sha256_hex>
//    DEL <section_id>
//    COMMIT <tx_id>
//
//  On commit, the writer:
//    1. fsync()s the WAL file
//    2. Atomically renames the new TTF file (write-to-tmp + rename)
//    3. Truncates the WAL
//
//  Readers see a consistent snapshot by:
//    1. Opening the base TTF file
//    2. Replaying the WAL up to the last COMMIT line they observed
//
//  This gives multi-reader / single-writer concurrency without locking.
// =============================================================================
#pragma once

#include "tts/types.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/ttf_writer.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tts {

struct WalOp {
    enum class Kind : std::uint8_t { Put, Del, Commit };
    Kind           kind;
    std::uint32_t  section_id   = 0;
    std::uint64_t  offset_chars = 0;
    std::uint64_t  length_chars = 0;
    std::string    sha256_hex;
    std::uint64_t  tx_id        = 0;
};

class TtfWal {
public:
    TtfWal() = default;
    // Mutex is non-movable, so we wrap it in a shared_ptr to allow moves.
    TtfWal(TtfWal&&) noexcept = default;
    TtfWal& operator=(TtfWal&&) noexcept = default;

    /// Open or create a WAL sidecar for `base_path` (which should end in .ttf).
    /// The WAL path is `base_path + ".wal"`.
    [[nodiscard]] static TtfResult<TtfWal> open(const std::filesystem::path& base_path);

    /// Append an op. Returns the WAL byte offset where the op was written.
    [[nodiscard]] TtfResult<std::uint64_t>
    append_put(std::uint32_t section_id,
               std::uint64_t offset_chars, std::uint64_t length_chars,
               std::string sha256_hex);

    [[nodiscard]] TtfResult<std::uint64_t>
    append_del(std::uint32_t section_id);

    /// Mark the current transaction as committed. Until commit, readers
    /// ignore all preceding ops in this WAL.
    [[nodiscard]] TtfStatus commit(std::uint64_t tx_id);

    /// Truncate the WAL after the last commit. Call this after the new
    /// TTF file has been atomically swapped in.
    [[nodiscard]] TtfStatus truncate();

    /// Read all ops up to (and including) the last committed transaction.
    /// Used by readers to build a consistent view.
    [[nodiscard]] std::vector<WalOp> read_committed() const;

    /// Path of the WAL file.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    mutable std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();
};

/// Atomic section replacement helper.
/// Writes `new_content` to a new TTF file, atomically swaps, and logs to WAL.
[[nodiscard]] TtfStatus
atomic_replace_section(const std::filesystem::path& ttf_path,
                       const TtfReader& current_state,
                       std::uint32_t section_id,
                       std::string_view new_content);

} // namespace tts
