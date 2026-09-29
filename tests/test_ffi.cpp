// Tests: C FFI surface
#include <gtest/gtest.h>
#include "tts/ffi.h"
#include "tts/ttf_writer.hpp"
#include "tts/attf_writer.hpp"
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path write_tmp_ttf(const std::string& name,
                                    const std::string& content) {
    auto tmp = std::filesystem::temp_directory_path() / name;
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();
    return tmp;
}

} // namespace

TEST(Ffi, VersionString) {
    const char* v = tts_version();
    ASSERT_NE(v, nullptr);
    EXPECT_NE(std::string{v}.find("3."), std::string::npos);
}

TEST(Ffi, TtfRoundTripThroughC) {
    tts::TtfWriter w;
    w.add_section(1, "hello from ffi");
    w.add_section(2, "second section");
    auto path = write_tmp_ttf("ffi_test.ttf", w.build());

    tts_ttf_reader_t reader = nullptr;
    int rc = tts_ttf_open(path.c_str(), &reader);
    ASSERT_EQ(rc, 0) << tts_last_error();
    ASSERT_NE(reader, nullptr);

    size_t n = 0;
    rc = tts_ttf_section_count(reader, &n);
    ASSERT_EQ(rc, 0);
    EXPECT_EQ(n, 2u);

    // Buffer too small: -2 and out_len set.
    std::vector<char> tiny(3);
    size_t need = 0;
    rc = tts_ttf_get_section(reader, 1, tiny.data(), tiny.size(), &need);
    EXPECT_EQ(rc, -2);
    EXPECT_EQ(need, std::string{"hello from ffi"}.size());

    // Real fetch.
    std::vector<char> buf(need);
    size_t got = 0;
    rc = tts_ttf_get_section(reader, 1, buf.data(), buf.size(), &got);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(got, need);
    const std::string section_str(buf.data(), got);
    EXPECT_EQ(section_str, std::string{"hello from ffi"});

    // Validation.
    uint32_t bad = 0;
    rc = tts_ttf_validate_all(reader, &bad);
    EXPECT_EQ(rc, 0);

    tts_ttf_close(reader);
    std::filesystem::remove(path);
}

TEST(Ffi, AttfRoundTripThroughC) {
    tts::AttfWriter w;
    w.add_setting("mem_zone", "active");
    w.add_page(1, "sys_prompt", "0x00A1", 9.0, "ffi attf page 1");
    w.add_page(2, "episodic",  "0x0BF2", 8.5, "ffi attf page 2");
    auto path = write_tmp_ttf("ffi_test.attf", w.build());

    tts_attf_reader_t reader = nullptr;
    int rc = tts_attf_open(path.c_str(), &reader);
    ASSERT_EQ(rc, 0) << tts_last_error();
    ASSERT_NE(reader, nullptr);

    size_t n = 0;
    rc = tts_attf_directory_count(reader, &n);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(n, 2u);

    std::vector<char> buf(64);
    size_t got = 0;
    rc = tts_attf_get_page(reader, "0x00A1", buf.data(), buf.size(), &got);
    EXPECT_EQ(rc, 0);
    const std::string p1(buf.data(), got);
    EXPECT_EQ(p1, std::string{"ffi attf page 1"});

    rc = tts_attf_get_page_by_id(reader, 2, buf.data(), buf.size(), &got);
    EXPECT_EQ(rc, 0);
    const std::string p2(buf.data(), got);
    EXPECT_EQ(p2, std::string{"ffi attf page 2"});

    tts_attf_close(reader);
    std::filesystem::remove(path);
}
