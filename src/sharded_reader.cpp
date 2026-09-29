// =============================================================================
//  src/sharded_reader.cpp — M7
// =============================================================================
#include "tts/sharded_reader.hpp"

#include <algorithm>

namespace tts {

TtfResult<ShardedReader>
ShardedReader::open(const std::filesystem::path& root_path,
                     std::string_view chain_suffix,
                     std::size_t max_shards) {
    ShardedReader sr;
    sr.root_path_ = root_path;

    auto first = TtfReader::open_file(root_path);
    if (!first) return std::unexpected(first.error());
    sr.shards_.push_back(std::move(*first));

    std::filesystem::path current = root_path;
    for (std::size_t i = 1; i < max_shards; ++i) {
        const std::uint32_t next_id = sr.shards_.back().metadata().next_tfd;
        if (next_id == 0) break;
        const std::string next_path =
            current.string() + std::string{chain_suffix};
        auto r = TtfReader::open_file(next_path);
        if (!r) break;  // missing or corrupt chain file — stop here
        current = next_path;
        sr.shards_.push_back(std::move(*r));
    }
    return sr;
}

TtfResult<std::string_view>
ShardedReader::get_section(std::uint32_t section_id) const noexcept {
    auto r = get_section_with_shard(section_id);
    if (!r) return std::unexpected(r.error());
    return r->second;
}

TtfResult<std::pair<std::size_t, std::string_view>>
ShardedReader::get_section_with_shard(std::uint32_t section_id) const noexcept {
    for (std::size_t i = 0; i < shards_.size(); ++i) {
        auto r = shards_[i].get_section(section_id);
        if (r) return std::make_pair(i, *r);
        if (r.error() != TtfError::SectionNotFound) return std::unexpected(r.error());
    }
    return std::unexpected(TtfError::SectionNotFound);
}

TtfResult<std::uint32_t> ShardedReader::validate_all() const noexcept {
    for (const auto& s : shards_) {
        auto v = s.validate_all();
        if (!v) return std::unexpected(v.error());
    }
    return std::uint32_t{0};
}

std::vector<std::uint32_t> ShardedReader::section_ids() const {
    std::vector<std::uint32_t> out;
    for (const auto& s : shards_) {
        auto ids = s.section_ids();
        out.insert(out.end(), ids.begin(), ids.end());
    }
    return out;
}

} // namespace tts
