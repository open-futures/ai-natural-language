# =============================================================================
#  TTS SDK — Text Tagged Specification (TTF & ATTF) v3.0
#  Root CMakeLists.txt
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(tts
    VERSION         3.5.2
    DESCRIPTION     "Text Tagged Specification SDK v3.5.2: TTF/ATTF with compression, encryption, semantic search, agent memory, KMS providers"
    HOMEPAGE_URL    "https://lotuschain.org"
    LANGUAGES       C CXX
)

# C++23 is required (std::expected, std::span, std::format)
set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Options
option(TTS_BUILD_TESTS   "Build the TTS unit-test suite"           ON)
option(TTS_BUILD_CLI     "Build the ttsinspect + ttslint CLI tools" ON)
option(TTS_BUILD_TUI     "Build the tts-tui interactive browser"   ON)
option(TTS_BUILD_SHARED  "Build TTS as a shared library (default STATIC)" OFF)
option(TTS_USE_MMAP      "Enable memory-mapped file backend"        ON)
option(TTS_USE_ZSTD      "Enable zstd compression"                  ON)
option(TTS_USE_AWS_KMS   "Compile AWS KMS key provider adapter"     OFF)
option(TTS_USE_GCP_KMS   "Compile GCP KMS key provider adapter"     OFF)
option(TTS_USE_VAULT     "Compile HashiCorp Vault key provider"     OFF)
option(TTS_WERROR        "Treat compiler warnings as errors"        OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    if(TTS_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align -Wno-unknown-pragmas
    )
    if(TTS_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# Platform detection
set(TTS_PLATFORM_POSIX  OFF)
set(TTS_PLATFORM_WINDOWS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
   CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
    set(TTS_PLATFORM_POSIX  ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(TTS_PLATFORM_WINDOWS ON)
else()
    message(WARNING "TTS: unrecognised platform '${CMAKE_SYSTEM_NAME}'")
endif()

include(FetchContent)

# CMake 4.0+ policy floor for older deps
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# yaml-cpp
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE
)
set(YAML_CPP_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB  OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL        OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

if(TTS_BUILD_CLI)
    FetchContent_Declare(
        CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(CLI11)
endif()

if(TTS_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_runtime ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(src)

if(TTS_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(TTS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "================ TTS SDK v3.5.2 ================")
message(STATUS " Version         : ${PROJECT_VERSION}")
message(STATUS " C++ standard    : ${CMAKE_CXX_STANDARD}")
message(STATUS " Library type    : ${TTS_BUILD_SHARED}")
message(STATUS " Build tests     : ${TTS_BUILD_TESTS}")
message(STATUS " Build CLI       : ${TTS_BUILD_CLI}")
message(STATUS " Build TUI       : ${TTS_BUILD_TUI}")
message(STATUS " mmap backend    : ${TTS_USE_MMAP}")
message(STATUS " zstd            : ${TTS_USE_ZSTD}")
message(STATUS " AWS KMS         : ${TTS_USE_AWS_KMS}")
message(STATUS " GCP KMS         : ${TTS_USE_GCP_KMS}")
message(STATUS " Vault           : ${TTS_USE_VAULT}")
message(STATUS " Platform (POSIX): ${TTS_PLATFORM_POSIX}")
message(STATUS " Platform (Win)  : ${TTS_PLATFORM_WINDOWS}")
message(STATUS "================================================")
