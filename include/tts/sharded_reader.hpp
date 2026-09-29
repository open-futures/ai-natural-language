// =============================================================================
//  include/tts/sharded_reader.hpp
//  Phase M7 — ShardedReader walks a `next_tfd` chain across multiple files.
//
//  Each shard is a regular TtfReader. The ShardedReader holds them in a
//  vector and exposes a unified get_section() that searches each shard in
//  order. Useful for very large corpora that don't fit in a single file.
// =============================================================================
#pragma once

#include "tts/ttf_reader.hpp"
#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace tts {

class ShardedReader {
public:
    ShardedReader() = default;

    /// Open `root_path` and follow its `next_tfd` chain, loading each shard
    /// into memory. Stops when the chain ends or after `max_shards` files.
    [[nodiscard]] static TtfResult<ShardedReader>
    open(const std::filesystem::path& root_path,
         std::string_view chain_suffix = ".next.ttf",
         std::size_t max_shards = 64);

    /// Look up a section across all shards. Returns the first match.
    [[nodiscard]] TtfResult<std::string_view>
    get_section(std::uint32_t section_id) const noexcept;

    /// Look up a section, returning which shard index it came from.
    [[nodiscard]] TtfResult<std::pair<std::size_t, std::string_view>>
    get_section_with_shard(std::uint32_t section_id) const noexcept;

    /// Validate every section in every shard.
    [[nodiscard]] TtfResult<std::uint32_t> validate_all() const noexcept;

    /// All section ids across all shards (in shard order).
    [[nodiscard]] std::vector<std::uint32_t> section_ids() const;

    /// Number of shards loaded.
    [[nodiscard]] std::size_t shard_count() const noexcept { return shards_.size(); }

    /// Access a specific shard by index.
    [[nodiscard]] const TtfReader& shard(std::size_t i) const { return shards_[i]; }

private:
    std::vector<TtfReader> shards_;
    std::filesystem::path  root_path_;
};

} // namespace tts
