// =============================================================================
//  include/tts/semantic_search.hpp
//  Phase M4 — Semantic vector search over ATTF pages.
//
//  Spec (ATTF v3.0): New index tag 402.
//    \x03402:<addr>,<float>,<float>,...,<float>\n
//
//  Each row stores one page's embedding as a comma-separated float list.
//  The metric and model metadata are stored in the header (\x04 line):
//    \x04mem_zone:active|last_sync:...|embedding_model:text-embedding-3-small|embedding_dim:1536|metric:cosine
//
//  Search API:
//    auto results = reader.search_semantic(query_embedding, {.top_k=10});
//
//  Three index implementations are supported, chosen by the user at
//  index-build time. The reader transparently uses whatever is stored.
// =============================================================================
#pragma once

#include "tts/attf_reader.hpp"
#include "tts/types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tts::semantic {

struct SearchResult {
    std::string  hex_address;
    float        score;
    std::string_view page_content;  // aliases the underlying MemoryView
};

struct SearchOpts {
    std::uint32_t top_k      = 10;
    float         min_score  = 0.0f;
    /// Optional filter — only return pages whose type matches any in the list.
    std::vector<std::string> type_filter;
};

/// Distance metric used by the index.
enum class Metric : std::uint8_t {
    Cosine = 0,
    Euclidean = 1,
    DotProduct = 2,
};

/// Index type — implementation detail, transparent to the caller.
enum class IndexType : std::uint8_t {
    Flat = 0,        // brute-force linear scan
    HNSW = 1,        // Hierarchical Navigable Small World (small inline impl)
    PQ_IVF = 2,      // Product Quantization + Inverted File (roadmap)
};

// -----------------------------------------------------------------------------
//  Parsed embedding entry (Tag 402 rows)
// -----------------------------------------------------------------------------
struct EmbeddingEntry {
    std::string            hex_address;
    std::vector<float>     vector;
};

/// Parse the Tag 402 rows out of an AttfReader.
/// Returns the parsed embeddings + the model/dim/metric header fields.
struct ParsedIndex {
    std::vector<EmbeddingEntry> entries;
    std::string                 model;
    std::uint32_t               dim    = 0;
    Metric                      metric = Metric::Cosine;
};

[[nodiscard]] ParsedIndex parse_index(const AttfReader& reader);

/// Compute the similarity between two vectors under the given metric.
[[nodiscard]] float
similarity(std::span<const float> a,
           std::span<const float> b,
           Metric metric) noexcept;

/// Brute-force flat search. Suitable for <10k pages.
[[nodiscard]] std::vector<SearchResult>
search_flat(const ParsedIndex& idx,
            std::span<const float> query,
            const AttfReader& reader,
            const SearchOpts& opts);

/// HNSW-lite search. Same API as search_flat but uses a small inline
/// navigable-small-world graph for approximate nearest-neighbor queries.
/// Suitable for 10k–1M pages.
[[nodiscard]] std::vector<SearchResult>
search_hnsw(const ParsedIndex& idx,
            std::span<const float> query,
            const AttfReader& reader,
            const SearchOpts& opts);

/// Hybrid search: combine Tag 401 keyword matches with Tag 402 vector matches.
[[nodiscard]] std::vector<SearchResult>
search_hybrid(const AttfReader& reader,
              const ParsedIndex& idx,
              std::span<const float> query_embedding,
              std::string_view keyword_query,
              float keyword_weight   = 0.3f,
              float semantic_weight  = 0.7f,
              const SearchOpts& opts = {});

} // namespace tts::semantic
