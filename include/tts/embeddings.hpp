// =============================================================================
//  include/tts/embeddings.hpp
//  Phase M4 — Embedding model interface + reference implementations.
//
//  The SDK does NOT ship a trained model. It ships an IEmbedder interface
//  and three reference implementations:
//
//    1. HashEmbedder      — deterministic hash-based embedder for testing.
//                           Produces 256-dim float vectors. No semantic
//                           value, but stable and zero-dep.
//
//    2. OpenAIEmbedder    — HTTP client for OpenAI's text-embedding-3-small.
//                           Requires an API key (env var OPENAI_API_KEY).
//                           Default for production use.
//
//    3. LocalEmbedder     — ONNX runtime + bge-small-en model. Offline-capable.
//                           Requires -DTTS_USE_ONNXRUNTIME=ON.
//
//  All embedders return std::vector<float>. Dim is determined by the model.
// =============================================================================
#pragma once

#include "tts/types.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tts::embeddings {

struct EmbeddingOptions {
    /// Truncate input to this many characters (model-dependent).
    std::uint32_t max_chars = 8192;
    /// Normalize the embedding to unit L2 norm (recommended for cosine).
    bool normalize = true;
};

class IEmbedder {
public:
    virtual ~IEmbedder() = default;
    /// Embed a single string. Returns a vector of floats.
    [[nodiscard]] virtual TtfResult<std::vector<float>>
    embed(std::string_view text, const EmbeddingOptions& opts = {}) const = 0;

    /// Model name (for the EmbeddingIndex metadata tag).
    [[nodiscard]] virtual std::string model_name() const = 0;
    /// Embedding dimensionality.
    [[nodiscard]] virtual std::uint32_t dim() const = 0;
};

/// Deterministic hash-based embedder for tests.
/// Maps each string to a 256-dim float vector deterministically.
class HashEmbedder : public IEmbedder {
public:
    explicit HashEmbedder(std::uint32_t dim = 256) : dim_(dim) {}

    [[nodiscard]] TtfResult<std::vector<float>>
    embed(std::string_view text, const EmbeddingOptions& opts = {}) const override;

    [[nodiscard]] std::string  model_name() const override { return "hash-deterministic"; }
    [[nodiscard]] std::uint32_t dim() const override       { return dim_; }

private:
    std::uint32_t dim_;
};

/// HTTP-based OpenAI embedder. v3.5: real implementation using POSIX sockets
/// (no libcurl). For HTTPS in production, front with stunnel or replace via
/// your own IEmbedder that uses your existing HTTP client.
class OpenAIEmbedder : public IEmbedder {
public:
    explicit OpenAIEmbedder(std::string api_key,
                             std::string model = "text-embedding-3-small");

    [[nodiscard]] TtfResult<std::vector<float>>
    embed(std::string_view text, const EmbeddingOptions& opts = {}) const override;

    [[nodiscard]] std::string  model_name() const override { return model_; }
    [[nodiscard]] std::uint32_t dim() const override;

private:
    std::string api_key_;
    std::string model_;
};

} // namespace tts::embeddings
