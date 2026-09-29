// =============================================================================
//  src/version.cpp — Phase 1 stub
//
//  Provides the only translation unit in the static library so that CMake
//  can build `libtts` before the real reader/writer sources land in later
//  phases. Real headers/implementations (types.hpp, memory_view.hpp,
//  ttf_reader.cpp, attf_reader.cpp, ...) will be added to src/CMakeLists.txt
//  incrementally.
// =============================================================================
#include "tts/version.hpp"

namespace tts {

namespace {
    constexpr std::string_view kVersionString = "3.5.2";
    constexpr std::uint32_t    kVersionMajor  = 3;
    constexpr std::uint32_t    kVersionMinor  = 5;
    constexpr std::uint32_t    kVersionPatch  = 2;
} // namespace

std::string_view version() noexcept {
    return kVersionString;
}

std::uint32_t version_packed() noexcept {
    return (kVersionMajor << 16) | (kVersionMinor << 8) | kVersionPatch;
}

} // namespace tts
