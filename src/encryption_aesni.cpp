// =============================================================================
//  src/encryption_aesni.cpp — v3.5
//
//  AES-NI hardware acceleration hooks for AES-256-GCM.
//
//  Philosophy #6: "The dependency surface is the API's portability surface."
//  AES-NI intrinsics are CPU features, NOT external dependencies. They
//  don't violate the "vendored crypto" principle — they're hardware
//  acceleration of the existing in-tree implementation.
//
//  Runtime detection via CPUID ensures we fall back to the portable C
//  implementation on non-x86_64 platforms (ARM, RISC-V, WASI, etc.).
//
//  v3.5 status: CPU feature detection is wired up and exposed via the
//  `has_aesni_hardware_acceleration()` function. Full AES-NI integration
//  (replacing the portable AES block cipher inner loop with _mm_aesenc_si128)
//  is on the v4.0 roadmap — v3.5 establishes the detection infrastructure
//  so callers can query and plan around it.
// =============================================================================
#include "tts/encryption.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#  include <immintrin.h>
#  define TTS_HAS_AESNI_PLATFORM 1
#else
#  define TTS_HAS_AESNI_PLATFORM 0
#endif

namespace tts::crypto::aes {

namespace {

struct CpuCaps {
    bool aesni = false;
    bool pclmulqdq = false;
};

[[nodiscard]] CpuCaps detect_cpu() noexcept {
    CpuCaps caps;
#if TTS_HAS_AESNI_PLATFORM
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    caps.aesni      = (ecx & (1u << 25)) != 0;
    caps.pclmulqdq  = (ecx & (1u <<  1)) != 0;
#else
    caps.aesni      = false;
    caps.pclmulqdq  = false;
#endif
    return caps;
}

[[nodiscard]] const CpuCaps& cpu_caps() noexcept {
    static const CpuCaps caps = detect_cpu();
    return caps;
}

} // namespace

[[nodiscard]] bool has_aesni_hardware_acceleration() noexcept {
    return cpu_caps().aesni && cpu_caps().pclmulqdq;
}

} // namespace tts::crypto::aes
