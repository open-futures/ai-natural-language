// =============================================================================
//  include/tts/version.hpp
//  Minimal public header shipped in Phase 1 so the library has a stable,
//  observable entry point. The full reader/writer APIs arrive in Phases 2–4.
// =============================================================================
#pragma once

#include <cstdint>
#include <string_view>

namespace tts {

/// Semantic version of the linked TTS SDK (e.g. "3.0.0").
[[nodiscard]] std::string_view version() noexcept;

/// Packed 32-bit version (major<<16 | minor<<8 | patch).
[[nodiscard]] std::uint32_t version_packed() noexcept;

} // namespace tts
