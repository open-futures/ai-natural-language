// =============================================================================
//  src/semantic_search.cpp — M4
// =============================================================================
#include "tts/semantic_search.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_set>

namespace tts::semantic {

namespace {

[[nodiscard]] std::vector<float> parse_float_csv(std::string_view s) {
    std::vector<float> out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = s.find(',', i);
        if (j == std::string_view::npos) j = s.size();
        const std::string token{s.substr(i, j - i)};
        if (!token.empty()) {
            try { out.push_back(std::stof(token)); }
            catch (...) { /* skip malformed */ }
        }
        i = (j == s.size()) ? s.size() : j + 1;
    }
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
//  parse_index
// -----------------------------------------------------------------------------
ParsedIndex parse_index(const AttfReader& reader) {
    ParsedIndex out;
    // Extract model/dim/metric from header settings.
    out.model  = reader.header().get_setting("embedding_model");
    const auto dim_str = reader.header().get_setting("embedding_dim");
    if (!dim_str.empty()) out.dim = static_cast<std::uint32_t>(std::stoul(dim_str));
    const auto metric_str = reader.header().get_setting("metric");
    if (metric_str == "euclidean") out.metric = Metric::Euclidean;
    else if (metric_str == "dotproduct") out.metric = Metric::DotProduct;
    else out.metric = Metric::Cosine;

    // Walk the directory to associate each page with its embedding.
    // The Tag 402 rows are interleaved with the page content in the
    // data stream, but the simplest source of truth is the directory
    // itself: for each directory entry, attempt to fetch a 402: row
    // with matching address. We don't currently store 402 rows in a
    // separate map on the reader (to keep the reader API minimal), so
    // we re-scan the buffer here.
    //
    // For the SDK reference, we accept embeddings stored as part of the
    // page metadata (rather than in 402: rows): a page whose content
    // starts with the literal "\x00" + "EMB:" prefix is interpreted as
    // containing a CSV float vector. This is a v3.0 convention used
    // by tts::agent::Memory::remember().
    for (const auto& e : reader.directory()) {
        auto page = reader.get_page(e.hex_address);
        if (!page) continue;
        if (page->size() < 5) continue;
        // Compare the first byte and the next 4 chars independently to
        // avoid the C++ hex-escape parsing gotcha (\x00E in source code
        // would be interpreted as a single char 0x0E).
        if ((*page)[0] != '\x00') continue;
        if (page->substr(1, 4) != "EMB:") continue;
        EmbeddingEntry ee;
        ee.hex_address = e.hex_address;
        ee.vector      = parse_float_csv(page->substr(5));
        out.entries.push_back(std::move(ee));
    }
    if (out.dim == 0 && !out.entries.empty()) {
        out.dim = static_cast<std::uint32_t>(out.entries[0].vector.size());
    }
    return out;
}

// -----------------------------------------------------------------------------
//  similarity
// -----------------------------------------------------------------------------
float similarity(std::span<const float> a,
                 std::span<const float> b,
                 Metric metric) noexcept {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    if (metric == Metric::DotProduct) {
        float s = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
        return s;
    }
    if (metric == Metric::Euclidean) {
        float s = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const float d = a[i] - b[i];
            s += d * d;
        }
        return -std::sqrt(s);  // higher = better
    }
    // Cosine
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na  += a[i] * a[i];
        nb  += b[i] * b[i];
    }
    if (na == 0.0f || nb == 0.0f) return 0.0f;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

// -----------------------------------------------------------------------------
//  Flat search
// -----------------------------------------------------------------------------
std::vector<SearchResult>
search_flat(const ParsedIndex& idx,
            std::span<const float> query,
            const AttfReader& reader,
            const SearchOpts& opts) {
    std::vector<SearchResult> all;
    all.reserve(idx.entries.size());
    std::unordered_set<std::string> type_filter_set(
        opts.type_filter.begin(), opts.type_filter.end());

    for (const auto& e : idx.entries) {
        // Find the corresponding directory entry to check the type filter.
        const AttfPageEntry* dir_entry = nullptr;
        for (const auto& de : reader.directory()) {
            if (de.hex_address == e.hex_address) { dir_entry = &de; break; }
        }
        if (dir_entry && !type_filter_set.empty() &&
            !type_filter_set.count(dir_entry->type)) {
            continue;
        }
        SearchResult sr;
        sr.hex_address = e.hex_address;
        sr.score       = similarity(query, e.vector, idx.metric);
        if (sr.score < opts.min_score) continue;
        auto page = reader.get_page(e.hex_address);
        if (page) sr.page_content = *page;
        all.push_back(std::move(sr));
    }
    // Sort by score descending
    std::sort(all.begin(), all.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    if (all.size() > opts.top_k) all.resize(opts.top_k);
    return all;
}

// -----------------------------------------------------------------------------
//  HNSW-lite
//
//  This is a minimal, correct-but-simple HNSW: we build a 1-level graph
//  (no hierarchy) where each node has M nearest neighbours as edges.
//  Search starts from a random entry point and greedily walks the graph
//  for `ef` candidates, returning top_k. This is approximate but adequate
//  for the SDK reference; production use should swap in hnswlib.
// -----------------------------------------------------------------------------
namespace {

struct HnswGraph {
    std::vector<std::vector<std::uint32_t>> neighbors;  // node -> [neighbor_idx,...]
};

[[nodiscard]] HnswGraph build_hnsw(const ParsedIndex& idx, std::uint32_t M) {
    HnswGraph g;
    g.neighbors.resize(idx.entries.size());
    // For each node, find its M nearest neighbours by brute force.
    for (std::uint32_t i = 0; i < idx.entries.size(); ++i) {
        std::vector<std::pair<float, std::uint32_t>> dists;
        dists.reserve(idx.entries.size());
        for (std::uint32_t j = 0; j < idx.entries.size(); ++j) {
            if (i == j) continue;
            const float s = similarity(idx.entries[i].vector,
                                        idx.entries[j].vector,
                                        Metric::Cosine);
            dists.emplace_back(s, j);
        }
        std::sort(dists.begin(), dists.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::uint32_t k = 0; k < M && k < dists.size(); ++k) {
            g.neighbors[i].push_back(dists[k].second);
        }
    }
    return g;
}

} // namespace

std::vector<SearchResult>
search_hnsw(const ParsedIndex& idx,
            std::span<const float> query,
            const AttfReader& reader,
            const SearchOpts& opts) {
    if (idx.entries.empty()) return {};
    constexpr std::uint32_t M  = 16;
    constexpr std::uint32_t ef = 64;
    const auto g = build_hnsw(idx, M);
    std::uint32_t entry = 0;
    // Find best entry point by random sampling (no real entry-point heuristic
    // in this minimal impl).
    float best_score = similarity(query, idx.entries[0].vector, idx.metric);
    for (std::size_t i = 1; i < idx.entries.size() && i < 64; ++i) {
        const float s = similarity(query, idx.entries[i].vector, idx.metric);
        if (s > best_score) { best_score = s; entry = static_cast<std::uint32_t>(i); }
    }
    // Greedy walk
    std::vector<std::pair<float, std::uint32_t>> candidates;
    std::vector<bool> visited(idx.entries.size(), false);
    visited[entry] = true;
    candidates.emplace_back(best_score, entry);
    std::size_t cursor = 0;
    while (cursor < candidates.size() && candidates.size() < ef) {
        const auto [score, node] = candidates[cursor++];
        for (const auto n : g.neighbors[node]) {
            if (visited[n]) continue;
            visited[n] = true;
            const float s = similarity(query, idx.entries[n].vector, idx.metric);
            if (candidates.size() < ef || s > candidates.front().first) {
                candidates.emplace_back(s, n);
                std::sort(candidates.begin(), candidates.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
                if (candidates.size() > ef) candidates.resize(ef);
            }
        }
    }
    // Convert to SearchResult
    std::vector<SearchResult> out;
    std::unordered_set<std::string> type_filter_set(
        opts.type_filter.begin(), opts.type_filter.end());
    for (const auto& [score, node] : candidates) {
        if (score < opts.min_score) continue;
        const auto& e = idx.entries[node];
        const AttfPageEntry* dir_entry = nullptr;
        for (const auto& de : reader.directory()) {
            if (de.hex_address == e.hex_address) { dir_entry = &de; break; }
        }
        if (dir_entry && !type_filter_set.empty() &&
            !type_filter_set.count(dir_entry->type)) {
            continue;
        }
        SearchResult sr;
        sr.hex_address = e.hex_address;
        sr.score       = score;
        auto page = reader.get_page(e.hex_address);
        if (page) sr.page_content = *page;
        out.push_back(std::move(sr));
        if (out.size() >= opts.top_k) break;
    }
    return out;
}

// -----------------------------------------------------------------------------
//  Hybrid search
// -----------------------------------------------------------------------------
std::vector<SearchResult>
search_hybrid(const AttfReader& reader,
              const ParsedIndex& idx,
              std::span<const float> query_embedding,
              std::string_view keyword_query,
              float keyword_weight,
              float semantic_weight,
              const SearchOpts& opts) {
    // Step 1: get semantic candidates (top 5x top_k).
    SearchOpts sem_opts = opts;
    sem_opts.top_k = opts.top_k * 5;
    auto sem = search_flat(idx, query_embedding, reader, sem_opts);

    // Step 2: keyword score via Tag 401 — for each directory entry, count
    // how many of its keywords are substrings of keyword_query.
    std::unordered_map<std::string, float> keyword_scores;
    for (const auto& s : reader.semantic_index()) {
        float score = 0.0f;
        for (const auto& kw : s.keywords) {
            if (keyword_query.find(kw) != std::string_view::npos) {
                score += 1.0f;
            }
        }
        keyword_scores[s.hex_address] = score;
    }

    // Step 3: combine
    std::vector<SearchResult> out;
    out.reserve(sem.size());
    for (auto& r : sem) {
        const float kw = keyword_scores.count(r.hex_address)
                         ? keyword_scores[r.hex_address] : 0.0f;
        r.score = keyword_weight * kw + semantic_weight * r.score;
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(),
              [](const SearchResult& a, const SearchResult& b) {
                  return a.score > b.score;
              });
    if (out.size() > opts.top_k) out.resize(opts.top_k);
    return out;
}

} // namespace tts::semantic
