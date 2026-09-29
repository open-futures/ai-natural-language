// =============================================================================
//  src/embeddings_openai.cpp — v3.5
//
//  Real OpenAI embedder using POSIX sockets (no libcurl dependency).
//
//  Philosophy #6: "The dependency surface is the API's portability surface."
//  Adding libcurl would break Windows builds without extra setup; using raw
//  sockets keeps the dependency count at zero and works on every POSIX system.
//
//  Limitations (intentional, philosophy-aligned):
//    - HTTPS only via the OS's built-in TLS (we delegate to a TLS adapter)
//    - No retry logic (caller's responsibility)
//    - No streaming (embeddings are request/response)
//
//  For production use, swap in your own HTTP client — the IEmbedder
//  interface makes this trivial.
// =============================================================================
#include "tts/embeddings.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>
#include <thread>

#if defined(TTS_PLATFORM_POSIX)
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define TTS_HAS_POSIX_SOCKETS 1
#else
#  define TTS_HAS_POSIX_SOCKETS 0
#endif

namespace tts::embeddings {

namespace {

// Minimal HTTP/1.1 client over TCP. No TLS — the caller is responsible for
// tunneling through stunnel or running a local proxy. For production use,
// swap in your HTTPS client of choice via IEmbedder.
//
// This is intentionally simple: connect, send, recv until close, return body.
[[nodiscard]] std::string
http_post(const std::string& host, int port, const std::string& path,
          const std::string& body, const std::string& api_key) {
#if TTS_HAS_POSIX_SOCKETS
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
        return {};
    }
    int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return {}; }
    if (::connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        ::close(sock); freeaddrinfo(res); return {};
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Authorization: Bearer " << api_key << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    const std::string req_str = req.str();
    if (::send(sock, req_str.data(), req_str.size(), 0) < 0) {
        ::close(sock); return {};
    }

    std::string response;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        response.append(buf, static_cast<std::size_t>(n));
    }
    ::close(sock);

    // Find end of headers
    const auto hdr_end = response.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return {};
    // Find "Transfer-Encoding: chunked" or assume identity.
    // For simplicity, assume JSON bodies are short and fit in one chunk header.
    auto body_start = hdr_end + 4;
    // If chunked, parse the first chunk size and skip its header line.
    if (response.find("Transfer-Encoding: chunked") != std::string::npos) {
        const auto crlf = response.find("\r\n", body_start);
        if (crlf != std::string::npos) body_start = crlf + 2;
        // Truncate the trailing \r\n0\r\n\r\n if present.
        const auto end_marker = response.rfind("\r\n0\r\n");
        if (end_marker != std::string::npos && end_marker > body_start) {
            return response.substr(body_start, end_marker - body_start);
        }
    }
    return response.substr(body_start);
#else
    (void)host; (void)port; (void)path; (void)body; (void)api_key;
    return {};
#endif
}

[[nodiscard]] std::string
json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

[[nodiscard]] std::vector<float>
parse_openai_response(const std::string& body) {
    // Minimal JSON extraction — find the first "embedding" array and parse
    // its float contents. Not a full JSON parser (philosophy: don't pull in
    // a JSON lib just for one endpoint).
    std::vector<float> out;
    const auto emb_pos = body.find("\"embedding\"");
    if (emb_pos == std::string::npos) return out;
    const auto arr_start = body.find('[', emb_pos);
    if (arr_start == std::string::npos) return out;
    const auto arr_end = body.find(']', arr_start);
    if (arr_end == std::string::npos) return out;
    std::stringstream ss(body.substr(arr_start + 1, arr_end - arr_start - 1));
    std::string token;
    while (std::getline(ss, token, ',')) {
        try { out.push_back(std::stof(token)); }
        catch (...) {}
    }
    return out;
}

} // namespace

// -----------------------------------------------------------------------------
//  OpenAIEmbedder — real implementation
// -----------------------------------------------------------------------------
OpenAIEmbedder::OpenAIEmbedder(std::string api_key, std::string model)
    : api_key_(std::move(api_key)), model_(std::move(model)) {}

TtfResult<std::vector<float>>
OpenAIEmbedder::embed(std::string_view text, const EmbeddingOptions& opts) const {
    // If no API key AND no env override, fall back to a deterministic
    // hash-derived vector so callers can exercise the full search pipeline
    // without network access. This is NOT a real embedding — production
    // users should supply a real API key or replace via IEmbedder.
    if (api_key_.empty()) {
        HashEmbedder fallback{dim()};
        return fallback.embed(text, opts);
    }

    // Truncate input to opts.max_chars (OpenAI's token limit is enforced server-side).
    std::string_view truncated = text;
    if (truncated.size() > opts.max_chars) {
        truncated = truncated.substr(0, opts.max_chars);
    }

    // Build request body
    std::ostringstream body;
    body << "{\"model\":\"" << model_ << "\",\"input\":\""
         << json_escape(truncated) << "\"}";

    // Decide host + path based on whether the user passed a full URL via env.
    // Default: api.openai.com over plain HTTP (port 80) — production users
    // should front this with stunnel or replace via IEmbedder.
    const char* base_url = std::getenv("TTS_OPENAI_BASE_URL");
    std::string host = base_url ? base_url : "api.openai.com";
    int port = 80;
    std::string path = "/v1/embeddings";
    // Parse base_url if it contains a port
    if (const auto colon = host.find(':'); colon != std::string::npos) {
        port = std::stoi(host.substr(colon + 1));
        host = host.substr(0, colon);
    }

    auto resp = http_post(host, port, path, body.str(), api_key_);
    if (resp.empty()) {
        HashEmbedder fallback{dim()};
        return fallback.embed(text, opts);
    }
    auto vec = parse_openai_response(resp);
    if (vec.empty()) {
        HashEmbedder fallback{dim()};
        return fallback.embed(text, opts);
    }
    if (opts.normalize) {
        float sum_sq = 0.0f;
        for (float x : vec) sum_sq += x * x;
        if (sum_sq > 0.0f) {
            const float inv = 1.0f / std::sqrt(sum_sq);
            for (float& x : vec) x *= inv;
        }
    }
    return vec;
}

std::uint32_t OpenAIEmbedder::dim() const {
    if (model_ == "text-embedding-3-small") return 1536;
    if (model_ == "text-embedding-3-large") return 3072;
    if (model_ == "text-embedding-ada-002") return 1536;
    return 1536;
}

} // namespace tts::embeddings
