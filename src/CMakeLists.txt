# =============================================================================
#  src/CMakeLists.txt — v3.0
# =============================================================================
if(TTS_BUILD_SHARED)
    add_library(tts SHARED)
else()
    add_library(tts STATIC)
endif()

target_sources(tts PRIVATE
    version.cpp
    types.cpp
    utf8_utils.cpp
    memory_view.cpp
    ttf_reader.cpp
    ttf_writer.cpp
    streaming_writer.cpp
    attf_reader.cpp
    attf_writer.cpp
    compression.cpp
    encryption.cpp
    encryption_aesni.cpp
    kms_providers.cpp
    wal.cpp
    embeddings.cpp
    embeddings_openai.cpp
    semantic_search.cpp
    agent_memory.cpp
    sharded_reader.cpp
    observability.cpp
    ffi.cpp
)

target_compile_features(tts PUBLIC cxx_std_23)

target_include_directories(tts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(TTS_USE_MMAP)
    target_compile_definitions(tts PUBLIC TTS_HAVE_MMAP=1)
endif()
if(TTS_PLATFORM_POSIX)
    target_compile_definitions(tts PUBLIC TTS_PLATFORM_POSIX=1)
endif()
if(TTS_PLATFORM_WINDOWS)
    target_compile_definitions(tts PUBLIC TTS_PLATFORM_WINDOWS=1)
endif()

# zstd (optional)
option(TTS_USE_ZSTD "Enable zstd compression" ON)
if(TTS_USE_ZSTD)
    FetchContent_Declare(
        zstd_src
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG        v1.5.6
        GIT_SHALLOW    TRUE
    )
    # We only need lib/ sources, so use Populate (not MakeAvailable) to avoid
    # building zstd's own CMake project.
    FetchContent_Populate(zstd_src)
    set(ZSTD_SOURCE_DIR ${zstd_src_SOURCE_DIR})
    add_library(zstd_static STATIC EXCLUDE_FROM_ALL
        ${ZSTD_SOURCE_DIR}/lib/common/debug.c
        ${ZSTD_SOURCE_DIR}/lib/common/entropy_common.c
        ${ZSTD_SOURCE_DIR}/lib/common/error_private.c
        ${ZSTD_SOURCE_DIR}/lib/common/fse_decompress.c
        ${ZSTD_SOURCE_DIR}/lib/common/pool.c
        ${ZSTD_SOURCE_DIR}/lib/common/threading.c
        ${ZSTD_SOURCE_DIR}/lib/common/xxhash.c
        ${ZSTD_SOURCE_DIR}/lib/common/zstd_common.c
        ${ZSTD_SOURCE_DIR}/lib/compress/fse_compress.c
        ${ZSTD_SOURCE_DIR}/lib/compress/hist.c
        ${ZSTD_SOURCE_DIR}/lib/compress/huf_compress.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_compress.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_literals.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_sequences.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_compress_superblock.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_double_fast.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_fast.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_lazy.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_ldm.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstd_opt.c
        ${ZSTD_SOURCE_DIR}/lib/compress/zstdmt_compress.c
        ${ZSTD_SOURCE_DIR}/lib/decompress/huf_decompress.c
        ${ZSTD_SOURCE_DIR}/lib/decompress/zstd_ddict.c
        ${ZSTD_SOURCE_DIR}/lib/decompress/zstd_decompress.c
        ${ZSTD_SOURCE_DIR}/lib/decompress/zstd_decompress_block.c
        ${ZSTD_SOURCE_DIR}/lib/dictBuilder/cover.c
        ${ZSTD_SOURCE_DIR}/lib/dictBuilder/divsufsort.c
        ${ZSTD_SOURCE_DIR}/lib/dictBuilder/fastcover.c
        ${ZSTD_SOURCE_DIR}/lib/dictBuilder/zdict.c
    )
    target_include_directories(zstd_static PUBLIC ${ZSTD_SOURCE_DIR}/lib)
    if(TTS_PLATFORM_POSIX)
        target_compile_definitions(zstd_static PUBLIC ZSTD_MULTITHREAD)
    endif()
    # Disable hand-written x86 ASM (requires a separate .S file) and use the
    # portable C fallback instead. This costs some performance but keeps the
    # build simple across platforms.
    target_compile_definitions(zstd_static PUBLIC ZSTD_DISABLE_ASM=1)
    target_link_libraries(tts PRIVATE zstd_static)
    target_compile_definitions(tts PUBLIC TTS_USE_ZSTD=1)
endif()

# yaml-cpp is PUBLIC because ttf_reader.hpp indirectly exposes YAML types
target_link_libraries(tts PUBLIC yaml-cpp)

set_target_properties(tts PROPERTIES
    VERSION         ${PROJECT_VERSION}
    SOVERSION       3
    DEBUG_POSTFIX   d
    POSITION_INDEPENDENT_CODE ON
)
