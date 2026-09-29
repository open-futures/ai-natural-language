// =============================================================================
//  src/embeddings.cpp — M4
// =============================================================================
#include "tts/embeddings.hpp"
#include "tts/sha256.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

namespace tts::embeddings {

// -----------------------------------------------------------------------------
//  HashEmbedder — deterministic, 0-dep, for tests
// -----------------------------------------------------------------------------
TtfResult<std::vector<float>>
HashEmbedder::embed(std::string_view text, const EmbeddingOptions& opts) const {
    // Produce a deterministic float vector by hashing successive 4-byte
    // windows of the input. The output is normalised to unit L2.
    std::vector<float> v(dim_, 0.0f);
    if (text.empty()) return v;
    auto digest = crypto::sha256(text);
    // Use the 32-byte digest to seed a PRNG, then sample dim_ floats.
    std::seed_seq seq(digest.begin(), digest.end());
    std::mt19937 gen(seq);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    for (std::uint32_t i = 0; i < dim_; ++i) v[i] = dist(gen);
    if (opts.normalize) {
        float sum_sq = 0.0f;
        for (float x : v) sum_sq += x * x;
        if (sum_sq > 0.0f) {
            const float inv = 1.0f / std::sqrt(sum_sq);
            for (float& x : v) x *= inv;
        }
    }
    return v;
}

// -----------------------------------------------------------------------------
//  OpenAIEmbedder — moved to src/embeddings_openai.cpp (v3.5)
//
//  The real implementation lives in embeddings_openai.cpp and uses POSIX
//  sockets (no libcurl dependency). This file retains only the HashEmbedder.
// -----------------------------------------------------------------------------

} // namespace tts::embeddings
