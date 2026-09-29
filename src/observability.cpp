// =============================================================================
//  src/observability.cpp — M7
// =============================================================================
#include "tts/observability.hpp"

namespace tts::obs {

namespace {
SpanSink g_sink;
}

void set_span_sink(SpanSink sink) { g_sink = std::move(sink); }
const SpanSink& span_sink() noexcept { return g_sink; }

ScopedSpan::ScopedSpan(std::string_view name, SpanKind kind)
    : span_{.name = name, .kind = kind} {
    span_.start = std::chrono::steady_clock::now();
}

ScopedSpan::~ScopedSpan() {
    span_.end = std::chrono::steady_clock::now();
    if (g_sink) g_sink(span_);
}

} // namespace tts::obs
