# =============================================================================
#  tests/CMakeLists.txt — v3.0 test suite
# =============================================================================
add_executable(tts_tests
    test_sha256.cpp
    test_utf8.cpp
    test_ttf_roundtrip.cpp
    test_attf_roundtrip.cpp
    test_ffi.cpp
    test_v3_features.cpp
    test_v35_features.cpp
    test_agent_memory.cpp
    test_sharding.cpp
)
target_link_libraries(tts_tests PRIVATE tts gtest_main)
target_compile_features(tts_tests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(tts_tests)
