// =============================================================================
//  include/tts/agent_memory.hpp
//  Phase M5 — High-level agent memory API.
//
//  Wraps the lower-level TTF/ATTF readers/writers into a friendly API that
//  AI engineers can drop into their agent loops without thinking about
//  tags, addresses, or embeddings.
// =============================================================================
#pragma once

#include "tts/attf_reader.hpp"
#include "tts/attf_writer.hpp"
#include "tts/embeddings.hpp"
#include "tts/semantic_search.hpp"
#include "tts/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tts::agent {

// Convert an AttfError into a TtfError for the typed result aliases.
// We map closely-related errors and fall back to InvalidArgument otherwise.
[[nodiscard]] inline TtfError attf_to_ttf(AttfError e) noexcept {
    switch (e) {
        case AttfError::Ok:                  return TtfError::Ok;
        case AttfError::InvalidMagicHeader:  return TtfError::InvalidMagicHeader;
        case AttfError::PageNotFound:        return TtfError::SectionNotFound;
        case AttfError::IoError:             return TtfError::IoError;
        case AttfError::InvalidArgument:     return TtfError::InvalidArgument;
        case AttfError::NotInitialized:      return TtfError::NotInitialized;
        default:                              return TtfError::InvalidArgument;
    }
}

struct RememberOpts {
    std::string                type      = "episodic";  // sys_prompt|semantic|episodic|procedural
    double                     priority  = 5.0;          // [0,10]
    std::vector<std::string>   keywords;
    /// If non-null, embeds the content and stores the vector alongside.
    /// If null, the page has no embedding (keyword-only retrieval).
    embeddings::IEmbedder*     embedder  = nullptr;
};

struct RecallQuery {
    /// For keyword-based recall: substring match against the semantic index.
    std::string                 task_description;
    /// For semantic recall: query embedding. If present, hybrid search runs.
    std::optional<std::vector<float>> query_embedding;
    /// Filter: only return pages with priority >= this.
    double                      min_priority = 0.0;
    /// Filter: only return pages whose type is in this list.
    std::vector<std::string>    type_filter;
    /// How many pages to return.
    std::uint32_t               top_k       = 10;
};

struct PageRef {
    std::uint32_t              page_id;
    std::string                hex_address;
    std::string                type;
    double                      priority;
    std::string_view           content;
    std::vector<std::string>   keywords;
};

class Memory {
public:
    /// Open an existing .attf memory file, or create it if missing.
    [[nodiscard]] static TtfResult<Memory> open(std::filesystem::path path);

    /// Append a new memory page. Computes the address, embeds (if embedder
    /// is set), updates the directory, and rewrites the file atomically.
    [[nodiscard]] TtfResult<PageRef>
    remember(std::string_view content, const RememberOpts& opts = {});

    /// Recall relevant pages.
    [[nodiscard]] TtfResult<std::vector<PageRef>>
    recall(const RecallQuery& query) const;

    /// Get a specific page by id.
    [[nodiscard]] TtfResult<PageRef> recall_by_id(std::uint32_t page_id) const;

    /// Get a specific page by hex address.
    [[nodiscard]] TtfResult<PageRef> recall_by_address(std::string_view hex_address) const;

    /// List the most recent N pages (highest page_id first).
    [[nodiscard]] TtfResult<std::vector<PageRef>> recall_recent(std::uint32_t n = 10) const;

    /// Re-embed all pages (call after changing the embedder).
    [[nodiscard]] TtfStatus reindex(embeddings::IEmbedder& embedder);

    /// Mark a page as forgotten (priority -> 0, type -> "forgotten").
    /// The content remains in the file for audit purposes.
    [[nodiscard]] TtfStatus forget(std::uint32_t page_id);

    /// Compact: rewrite the file, dropping pages with priority == 0.
    [[nodiscard]] TtfStatus consolidate();

    /// Path of the underlying .attf file.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path  path_;
    AttfReader             reader_;
    std::vector<AttfPageEntry> directory_snapshot_;
};

} // namespace tts::agent
