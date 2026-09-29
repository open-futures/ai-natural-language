# =============================================================================
#  cli/CMakeLists.txt — ttsinspect + ttslint + tts-tui CLI tools (v3.5)
# =============================================================================

# --- ttsinspect: inspect / validate / query-section / query-page / tui -----
add_executable(ttsinspect main.cpp)
target_link_libraries(ttsinspect PRIVATE tts CLI11::CLI11)
set_target_properties(ttsinspect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# --- ttslint: validate against v3.0 spec ----------------------------------
add_executable(ttslint lint.cpp)
target_link_libraries(ttslint PRIVATE tts CLI11::CLI11)
set_target_properties(ttslint PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

# --- tts-tui: interactive terminal UI (v3.5) -----------------------------
# Optional — only built if FTXUI fetches successfully. Gated behind option.
option(TTS_BUILD_TUI "Build the tts-tui interactive browser" ON)
if(TTS_BUILD_TUI)
    FetchContent_Declare(
        ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
        GIT_TAG        v5.0.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ftxui)

    # TUI sources compiled into ttsinspect so the `tui` subcommand is
    # available in the same binary. We also build a standalone tts-tui
    # binary for users who want a smaller tool.
    target_sources(ttsinspect PRIVATE tui.cpp)
    target_link_libraries(ttsinspect PRIVATE ftxui::component)

    add_executable(tts-tui tui.cpp)
    target_compile_definitions(tts-tui PRIVATE TTS_TUI_STANDALONE=1)
    target_link_libraries(tts-tui PRIVATE tts ftxui::component CLI11::CLI11)
    set_target_properties(tts-tui PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
endif()
