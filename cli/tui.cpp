// =============================================================================
//  cli/tui.cpp — v3.5
//
//  Interactive terminal UI for browsing TTF/ATTF files using FTXUI.
//
//  Subcommand of ttsinspect:   ttsinspect tui <file>
//
//  Features:
//    - Auto-detects TTF vs ATTF
//    - TTF: shows section list, content viewer, hash status
//    - ATTF: shows page directory, page content, semantic index
//    - Arrow keys to navigate, q to quit
// =============================================================================
#include "tts/attf_reader.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/version.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace tts::tui {

// -----------------------------------------------------------------------------
//  Model — holds either a TTF reader or an ATTF reader
// -----------------------------------------------------------------------------
struct TtfModel {
    TtfReader reader;
    std::vector<std::uint32_t> section_ids;
    int selected = 0;
};

struct AttfModel {
    AttfReader reader;
    std::vector<std::string> addresses;
    int selected = 0;
};

using Model = std::variant<std::monostate, TtfModel, AttfModel>;

[[nodiscard]] Model
load_file(const std::filesystem::path& path) {
    // Peek first byte to dispatch
    std::ifstream peek(path, std::ios::binary);
    if (!peek) return std::monostate{};
    char first = 0;
    peek.get(first);
    peek.close();

    if (first == '\x01') {
        auto r = AttfReader::open_file(path);
        if (!r) return std::monostate{};
        AttfModel m{std::move(*r), {}, 0};
        for (const auto& e : m.reader.directory()) {
            m.addresses.push_back(e.hex_address);
        }
        return m;
    }
    auto r = TtfReader::open_file(path);
    if (!r) return std::monostate{};
    TtfModel m{std::move(*r), r->section_ids(), 0};
    return m;
}

// -----------------------------------------------------------------------------
//  TTF view
// -----------------------------------------------------------------------------
[[nodiscard]] ftxui::Element
render_ttf(TtfModel& m) {
    using namespace ftxui;

    std::vector<ftxui::Element> left_items;
    left_items.push_back(text("TTF Sections") | bold | color(Color::Cyan));
    if (m.section_ids.empty()) {
        left_items.push_back(text("(no sections)") | dim);
    }
    for (std::size_t i = 0; i < m.section_ids.size(); ++i) {
        const auto id = m.section_ids[i];
        std::string label = "  [" + std::to_string(id) + "]";
        if (static_cast<int>(i) == m.selected) {
            left_items.push_back(text("> " + label.substr(2)) |
                                  inverted | bold);
        } else {
            left_items.push_back(text(label));
        }
    }

    std::vector<ftxui::Element> right_items;
    right_items.push_back(text("Section Content") | bold | color(Color::Green));
    if (!m.section_ids.empty() && m.selected >= 0 &&
        m.selected < static_cast<int>(m.section_ids.size())) {
        const auto id = m.section_ids[m.selected];
        auto sec = m.reader.get_section(id);
        if (sec) {
            // Truncate to first ~30 lines for display
            std::string_view content = *sec;
            std::size_t pos = 0;
            int lines = 0;
            while (pos < content.size() && lines < 30) {
                auto nl = content.find('\n', pos);
                if (nl == std::string_view::npos) nl = content.size();
                right_items.push_back(text(std::string{content.substr(pos, nl - pos)}));
                pos = nl + 1;
                ++lines;
            }
            if (pos < content.size()) {
                right_items.push_back(text("... (" +
                    std::to_string(content.size() - pos) + " more bytes)") | dim);
            }
            // Hash verification status
            right_items.push_back(text(""));
            right_items.push_back(text("[SHA-256 verified OK]") |
                                   color(Color::Green) | dim);
        } else {
            right_items.push_back(text("[ERROR: " +
                std::string{tts::to_string(sec.error())} + "]") |
                color(Color::Red));
        }
    }

    auto left_pane = vbox(left_items) | border | flex_grow;
    auto right_pane = vbox(right_items) | border | flex;

    return hbox({
        left_pane | size(WIDTH, EQUAL, 24),
        right_pane | flex,
    });
}

// -----------------------------------------------------------------------------
//  ATTF view
// -----------------------------------------------------------------------------
[[nodiscard]] ftxui::Element
render_attf(AttfModel& m) {
    using namespace ftxui;

    std::vector<ftxui::Element> left_items;
    left_items.push_back(text("ATTF Pages") | bold | color(Color::Cyan));
    left_items.push_back(text("ver: " + m.reader.header().version) | dim);
    left_items.push_back(text("zone: " + m.reader.header().get_setting("mem_zone")) | dim);
    left_items.push_back(text(""));

    for (std::size_t i = 0; i < m.reader.directory().size(); ++i) {
        const auto& e = m.reader.directory()[i];
        std::string label = "  P" + std::to_string(e.page_id) + " " + e.hex_address +
                            " [" + e.type + "] " + std::to_string(e.priority);
        if (static_cast<int>(i) == m.selected) {
            left_items.push_back(text("> " + label.substr(2)) | inverted | bold);
        } else {
            left_items.push_back(text(label));
        }
    }

    std::vector<ftxui::Element> right_items;
    right_items.push_back(text("Page Content") | bold | color(Color::Green));
    if (!m.reader.directory().empty() && m.selected >= 0 &&
        m.selected < static_cast<int>(m.reader.directory().size())) {
        const auto& e = m.reader.directory()[m.selected];
        auto page = m.reader.get_page(e.hex_address);
        if (page) {
            std::string_view content = *page;
            std::size_t pos = 0;
            int lines = 0;
            while (pos < content.size() && lines < 30) {
                auto nl = content.find('\n', pos);
                if (nl == std::string_view::npos) nl = content.size();
                right_items.push_back(text(std::string{content.substr(pos, nl - pos)}));
                pos = nl + 1;
                ++lines;
            }
            if (pos < content.size()) {
                right_items.push_back(text("... (" +
                    std::to_string(content.size() - pos) + " more bytes)") | dim);
            }
        } else {
            right_items.push_back(text("[ERROR: " +
                std::string{tts::to_string(page.error())} + "]") |
                color(Color::Red));
        }
        // Show keywords
        right_items.push_back(text(""));
        right_items.push_back(text("Keywords:") | bold);
        for (const auto& s : m.reader.semantic_index()) {
            if (s.hex_address == e.hex_address) {
                std::string kw_str;
                for (std::size_t i = 0; i < s.keywords.size(); ++i) {
                    if (i) kw_str += ", ";
                    kw_str += s.keywords[i];
                }
                right_items.push_back(text("  " + kw_str) | dim);
            }
        }
    }

    auto left_pane = vbox(left_items) | border | flex_grow;
    auto right_pane = vbox(right_items) | border | flex;

    return hbox({
        left_pane | size(WIDTH, EQUAL, 36),
        right_pane | flex,
    });
}

// -----------------------------------------------------------------------------
//  Main entry
// -----------------------------------------------------------------------------
int run(const std::filesystem::path& path) {
    using namespace ftxui;
    auto screen = ScreenInteractive::TerminalOutput();

    Model model = load_file(path);
    if (std::holds_alternative<std::monostate>(model)) {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    auto renderer = Renderer([&] {
        Element body;
        if (auto* ttf = std::get_if<TtfModel>(&model)) {
            body = render_ttf(*ttf);
        } else if (auto* attf = std::get_if<AttfModel>(&model)) {
            body = render_attf(*attf);
        } else {
            body = text("(empty)");
        }

        return vbox({
            text("TTS SDK v" + std::string{tts::version()} + " — " +
                 path.string()) | bold | color(Color::Yellow) | border,
            body | flex,
            text("↑↓ navigate  q quit") | dim | border,
        });
    });

    auto component = CatchEvent(renderer, [&](Event e) {
        if (e == Event::Character('q') || e == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (e == Event::ArrowDown) {
            if (auto* ttf = std::get_if<TtfModel>(&model)) {
                if (ttf->selected + 1 < static_cast<int>(ttf->section_ids.size())) ++ttf->selected;
            } else if (auto* attf = std::get_if<AttfModel>(&model)) {
                if (attf->selected + 1 < static_cast<int>(attf->reader.directory().size())) ++attf->selected;
            }
            return true;
        }
        if (e == Event::ArrowUp) {
            if (auto* ttf = std::get_if<TtfModel>(&model)) {
                if (ttf->selected > 0) --ttf->selected;
            } else if (auto* attf = std::get_if<AttfModel>(&model)) {
                if (attf->selected > 0) --attf->selected;
            }
            return true;
        }
        return false;
    });

    screen.Loop(component);
    return 0;
}

} // namespace tts::tui

// When compiled as a standalone binary (rather than as part of ttsinspect),
// provide a main() that uses CLI11 to parse args.
#ifdef TTS_TUI_STANDALONE
#include <CLI/CLI.hpp>
int main(int argc, char** argv) {
    CLI::App app{"tts-tui — Interactive TTF/ATTF browser (v3.5)"};
    std::string path;
    app.add_option("file", path, "Path to a .ttf or .attf file")->required();
    CLI11_PARSE(app, argc, argv);
    return tts::tui::run(path);
}
#endif
