// =============================================================================
//  include/tts/observability.hpp
//  Phase M7 — OpenTelemetry hooks.
//
//  The SDK emits spans for major operations. By default these go nowhere;
//  callers can install a callback to forward to OTel, Prometheus, etc.
// =============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace tts::obs {

enum class SpanKind : std::uint8_t {
    Internal = 0,
    Read     = 1,
    Write    = 2,
    Search   = 3,
};

struct Span {
    std::string_view     name;
    SpanKind             kind    = SpanKind::Internal;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point end   = {};
    std::int64_t         bytes_in  = 0;
    std::int64_t         bytes_out = 0;
    bool                 ok        = true;
};

using SpanSink = std::function<void(const Span&)>;

/// Install a global span sink. Pass nullptr to disable.
void set_span_sink(SpanSink sink);

/// Get the current global span sink (may be null).
[[nodiscard]] const SpanSink& span_sink() noexcept;

/// RAII helper: starts a span on construction, ends on destruction.
class ScopedSpan {
public:
    explicit ScopedSpan(std::string_view name, SpanKind kind = SpanKind::Internal);
    ~ScopedSpan();
    ScopedSpan(const ScopedSpan&)            = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

    ScopedSpan& set_bytes_in(std::int64_t n)  { span_.bytes_in  = n; return *this; }
    ScopedSpan& set_bytes_out(std::int64_t n) { span_.bytes_out = n; return *this; }
    ScopedSpan& set_ok(bool ok)               { span_.ok = ok;     return *this; }

private:
    Span span_;
};

} // namespace tts::obs
