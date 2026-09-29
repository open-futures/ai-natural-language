// =============================================================================
//  cli/main.cpp — Phase 5 CLI tool (`ttsinspect`)
//
//  Subcommands:
//    inspect       <file>                 — show high-level summary
//    validate      <file>                 — verify all section hashes (TTF) / page bytes
//    query-section <file> <section_id>    — TTF: print section content (zero-copy)
//    query-page    <file> <hex_address>   — ATTF: print page content
// =============================================================================
#include "tts/attf_reader.hpp"
#include "tts/ttf_reader.hpp"
#include "tts/types.hpp"
#include "tts/version.hpp"

#include <CLI/CLI.hpp>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace tts::tui {
int run(const std::filesystem::path& path);
}

namespace {

// Kept for callers that prefer building from an istream; not currently
// used by the CLI subcommands.
[[maybe_unused]] std::string
read_stream_to_string(std::istream& in) {
    std::string out;
    char buf[64 * 1024];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        out.append(buf, static_cast<std::size_t>(in.gcount()));
    }
    return out;
}

int do_inspect(const std::string& path) {
    // Peek first byte to dispatch TTF vs ATTF.
    std::ifstream peek(path, std::ios::binary);
    if (!peek) { std::cerr << "error: cannot open '" << path << "'\n"; return 1; }
    char first = 0;
    peek.get(first);
    peek.close();

    if (first == '\x01') {
        // ATTF
        auto r = tts::AttfReader::open_file(path);
        if (!r) {
            std::cerr << "ATTF open failed: " << tts::to_string(r.error()) << "\n";
            return 1;
        }
        std::cout << "ATTF file: " << path << "\n";
        std::cout << "Version   : " << r->header().version << "\n";
        for (const auto& [k, v] : r->header().settings) {
            std::cout << "Setting   : " << k << " = " << v << "\n";
        }
        std::cout << "Directory (" << r->directory().size() << " entries):\n";
        for (const auto& e : r->directory()) {
            std::cout << "  - id=" << e.page_id
                      << " type=" << e.type
                      << " addr=" << e.hex_address
                      << " prio=" << e.priority << "\n";
        }
        std::cout << "Semantic index (" << r->semantic_index().size() << " entries):\n";
        for (const auto& s : r->semantic_index()) {
            std::cout << "  - " << s.hex_address << " : [";
            for (std::size_t i = 0; i < s.keywords.size(); ++i) {
                if (i) std::cout << ", ";
                std::cout << s.keywords[i];
            }
            std::cout << "]\n";
        }
        return 0;
    }

    // TTF (default)
    auto r = tts::TtfReader::open_file(path);
    if (!r) {
        std::cerr << "TTF open failed: " << tts::to_string(r.error()) << "\n";
        return 1;
    }
    std::cout << "TTF file: " << path << "\n";
    std::cout << "TFD id    : " << r->metadata().tfd_id << "\n";
    std::cout << "Next TFD  : " << r->metadata().next_tfd << "\n";
    std::cout << "Encoding  : " << r->metadata().encoding << "\n";
    std::cout << "Sections  : " << r->metadata().sections.size() << "\n";
    std::cout << "Data stream: " << r->data_stream().size() << " bytes\n";
    for (const auto& s : r->metadata().sections) {
        std::cout << "  - section " << s.section_id
                  << "  off=" << s.offset_chars
                  << "  len=" << s.length_chars
                  << "  sha256=" << s.sha256_hex.substr(0, 16) << "...\n";
    }
    return 0;
}

int do_validate(const std::string& path) {
    // Try TTF first; fall back to ATTF.
    auto ttf = tts::TtfReader::open_file(path);
    if (ttf) {
        std::cout << "Validating TTF file: " << path << "\n";
        bool ok = true;
        for (const auto& s : ttf->metadata().sections) {
            auto r = ttf->get_section(s.section_id);
            if (!r) {
                std::cerr << "  section " << s.section_id
                          << " FAILED: " << tts::to_string(r.error()) << "\n";
                ok = false;
            } else {
                std::cout << "  section " << s.section_id << " OK (" << r->size() << " bytes)\n";
            }
        }
        return ok ? 0 : 2;
    }

    auto attf = tts::AttfReader::open_file(path);
    if (attf) {
        std::cout << "Validating ATTF file: " << path << "\n";
        bool ok = true;
        for (const auto& e : attf->directory()) {
            auto r = attf->get_page(e.hex_address);
            if (!r) {
                std::cerr << "  page " << e.hex_address
                          << " FAILED: " << tts::to_string(r.error()) << "\n";
                ok = false;
            } else {
                std::cout << "  page " << e.hex_address << " OK (" << r->size() << " bytes)\n";
            }
        }
        return ok ? 0 : 2;
    }

    std::cerr << "Not a TTF or ATTF file: " << path << "\n";
    std::cerr << "  TTF error : " << tts::to_string(ttf.error()) << "\n";
    std::cerr << "  ATTF error: " << tts::to_string(attf.error()) << "\n";
    return 1;
}

int do_query_section(const std::string& path, std::uint32_t section_id) {
    auto r = tts::TtfReader::open_file(path);
    if (!r) {
        std::cerr << "TTF open failed: " << tts::to_string(r.error()) << "\n";
        return 1;
    }
    auto s = r->get_section(section_id);
    if (!s) {
        std::cerr << "get_section(" << section_id << ") failed: "
                  << tts::to_string(s.error()) << "\n";
        return 1;
    }
    std::cout.write(s->data(), static_cast<std::streamsize>(s->size()));
    if (!s->empty() && s->back() != '\n') std::cout << "\n";
    return 0;
}

int do_query_page(const std::string& path, const std::string& hex_addr) {
    auto r = tts::AttfReader::open_file(path);
    if (!r) {
        std::cerr << "ATTF open failed: " << tts::to_string(r.error()) << "\n";
        return 1;
    }
    auto p = r->get_page(hex_addr);
    if (!p) {
        std::cerr << "get_page(" << hex_addr << ") failed: "
                  << tts::to_string(p.error()) << "\n";
        return 1;
    }
    std::cout.write(p->data(), static_cast<std::streamsize>(p->size()));
    if (!p->empty() && p->back() != '\n') std::cout << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"TTS SDK — TTF/ATTF inspector (v" + std::string{tts::version()} + ")"};
    app.require_subcommand(1);

    auto* inspect = app.add_subcommand("inspect", "Show high-level file summary");
    std::string inspect_path;
    inspect->add_option("file", inspect_path, "Path to a .ttf or .attf file")->required();

    auto* validate = app.add_subcommand("validate", "Verify hashes / integrity");
    std::string validate_path;
    validate->add_option("file", validate_path, "Path to verify")->required();

    auto* qs = app.add_subcommand("query-section", "TTF: print a section's content");
    std::string qs_path; std::uint32_t qs_id = 0;
    qs->add_option("file", qs_path, "TTF file")->required();
    qs->add_option("--id", qs_id, "Section id")->required();

    auto* qp = app.add_subcommand("query-page", "ATTF: print a page's content");
    std::string qp_path; std::string qp_addr;
    qp->add_option("file", qp_path, "ATTF file")->required();
    qp->add_option("--addr", qp_addr, "Hex address (e.g. 0x00A1)")->required();

    auto* tui = app.add_subcommand("tui", "Interactive terminal UI browser");
    std::string tui_path;
    tui->add_option("file", tui_path, "Path to a .ttf or .attf file")->required();

    CLI11_PARSE(app, argc, argv);

    if (app.got_subcommand(inspect)) return do_inspect(inspect_path);
    if (app.got_subcommand(validate)) return do_validate(validate_path);
    if (app.got_subcommand(qs))       return do_query_section(qs_path, qs_id);
    if (app.got_subcommand(qp))       return do_query_page(qp_path, qp_addr);
    if (app.got_subcommand(tui))     return tts::tui::run(tui_path);

    std::cerr << "No subcommand selected. Run with --help for usage.\n";
    return 1;
}
