// =============================================================================
//  cli/lint.cpp — ttslint: validates TTF/ATTF files against the v3.0 spec.
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

namespace {

int lint_ttf(const std::string& path) {
    auto r = tts::TtfReader::open_file(path);
    if (!r) {
        std::cerr << "ERROR: " << tts::to_string(r.error()) << "\n";
        return 1;
    }
    int warnings = 0;
    int errors  = 0;

    // Check 1: encoding is "UTF-8"
    if (r->metadata().encoding != "UTF-8") {
        std::cout << "WARNING: encoding is '" << r->metadata().encoding
                  << "', expected 'UTF-8'\n";
        ++warnings;
    }

    // Check 2: every section has a non-empty sha256
    for (const auto& s : r->metadata().sections) {
        if (s.sha256_hex.empty()) {
            std::cout << "WARNING: section " << s.section_id
                      << " has no sha256 — integrity check disabled\n";
            ++warnings;
        }
        if (s.sha256_hex.size() != 64) {
            std::cout << "ERROR: section " << s.section_id
                      << " sha256 length is " << s.sha256_hex.size()
                      << ", expected 64\n";
            ++errors;
        }
    }

    // Check 3: hash verification passes
    for (const auto& s : r->metadata().sections) {
        auto sec = r->get_section(s.section_id);
        if (!sec) {
            std::cout << "ERROR: section " << s.section_id
                      << " verification failed: " << tts::to_string(sec.error()) << "\n";
            ++errors;
        }
    }

    // Check 4: section ids are unique
    std::vector<std::uint32_t> ids = r->section_ids();
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] == ids[i-1]) {
            std::cout << "ERROR: duplicate section id " << ids[i] << "\n";
            ++errors;
        }
    }

    // Check 5: next_tfd chain target exists if non-zero
    if (r->metadata().next_tfd != 0) {
        const std::string chain = path + ".next.ttf";
        std::ifstream f(chain);
        if (!f) {
            std::cout << "WARNING: next_tfd=" << r->metadata().next_tfd
                      << " but chain file '" << chain << "' does not exist\n";
            ++warnings;
        }
    }

    std::cout << (errors ? "FAIL" : "OK") << ": " << path
              << " (" << warnings << " warnings, " << errors << " errors)\n";
    return errors ? 1 : 0;
}

int lint_attf(const std::string& path) {
    auto r = tts::AttfReader::open_file(path);
    if (!r) {
        std::cerr << "ERROR: " << tts::to_string(r.error()) << "\n";
        return 1;
    }
    int warnings = 0;
    int errors  = 0;

    // Check 1: version is 2.0 or 3.0
    if (r->header().version != "2.0" && r->header().version != "3.0") {
        std::cout << "WARNING: version is '" << r->header().version
                  << "', expected '2.0' or '3.0'\n";
        ++warnings;
    }

    // Check 2: every directory entry's page is fetchable
    for (const auto& e : r->directory()) {
        auto page = r->get_page(e.hex_address);
        if (!page) {
            std::cout << "ERROR: page " << e.hex_address
                      << " (id " << e.page_id << ") is unfetchable: "
                      << tts::to_string(page.error()) << "\n";
            ++errors;
        }
    }

    // Check 3: page ids are unique
    std::vector<std::uint32_t> ids;
    for (const auto& e : r->directory()) ids.push_back(e.page_id);
    std::sort(ids.begin(), ids.end());
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (ids[i] == ids[i-1]) {
            std::cout << "ERROR: duplicate page id " << ids[i] << "\n";
            ++errors;
        }
    }

    // Check 4: priority is in [0, 10]
    for (const auto& e : r->directory()) {
        if (e.priority < 0.0 || e.priority > 10.0) {
            std::cout << "WARNING: page " << e.hex_address
                      << " priority " << e.priority << " is outside [0,10]\n";
            ++warnings;
        }
    }

    // Check 5: every page address in the directory is unique
    std::vector<std::string> addrs;
    for (const auto& e : r->directory()) addrs.push_back(e.hex_address);
    std::sort(addrs.begin(), addrs.end());
    for (std::size_t i = 1; i < addrs.size(); ++i) {
        if (addrs[i] == addrs[i-1]) {
            std::cout << "ERROR: duplicate page address " << addrs[i] << "\n";
            ++errors;
        }
    }

    std::cout << (errors ? "FAIL" : "OK") << ": " << path
              << " (" << warnings << " warnings, " << errors << " errors)\n";
    return errors ? 1 : 0;
}

} // namespace

int lint_main(int argc, char** argv) {
    CLI::App app{"ttslint — TTS v3.0 file linter"};
    std::string path;
    app.add_option("file", path, "Path to a .ttf or .attf file")->required();
    CLI11_PARSE(app, argc, argv);

    // Peek the first byte to decide TTF vs ATTF.
    std::ifstream peek(path, std::ios::binary);
    if (!peek) { std::cerr << "ERROR: cannot open '" << path << "'\n"; return 1; }
    char first = 0;
    peek.get(first);
    peek.close();

    if (first == '\x01') return lint_attf(path);
    return lint_ttf(path);
}

int main(int argc, char** argv) {
    return lint_main(argc, argv);
}
