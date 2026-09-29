// Tests: tts::utf8 helpers
#include <gtest/gtest.h>
#include "tts/utf8_utils.hpp"
#include <string>

using namespace tts;

TEST(Utf8, AsciiCount) {
    EXPECT_EQ(utf8::count_chars("hello"), 5u);
    EXPECT_EQ(utf8::count_chars(""), 0u);
}

TEST(Utf8, MultibyteCount) {
    // "héllo" = h(1) + é(2 bytes) + l(1) + l(1) + o(1) = 5 code points, 6 bytes
    const std::string s = "héllo";
    EXPECT_EQ(s.size(), 6u);
    EXPECT_EQ(utf8::count_chars(s), 5u);
}

TEST(Utf8, CjkCount) {
    // 3 Chinese chars, each 3 bytes in UTF-8
    const std::string s = "你好世";
    EXPECT_EQ(s.size(), 9u);
    EXPECT_EQ(utf8::count_chars(s), 3u);
}

TEST(Utf8, EmojiCount) {
    // 4-byte code point (U+1F600,😀)
    const std::string s = "😀";
    EXPECT_EQ(s.size(), 4u);
    EXPECT_EQ(utf8::count_chars(s), 1u);
}

TEST(Utf8, CharOffsetToByte) {
    const std::string s = "héllo😀";
    //                  byte: 0 1 2 3 4 5 6 7 8 9 10
    //                  char: 0   1   2 3 4   5
    EXPECT_EQ(*utf8::char_offset_to_byte(s, 0), 0u);
    EXPECT_EQ(*utf8::char_offset_to_byte(s, 1), 1u);
    EXPECT_EQ(*utf8::char_offset_to_byte(s, 2), 3u);
    EXPECT_EQ(*utf8::char_offset_to_byte(s, 5), 6u);
    EXPECT_EQ(*utf8::char_offset_to_byte(s, 6), 10u); // one-past-end
}

TEST(Utf8, SubstrChars) {
    const std::string s = "héllo😀";
    auto r = utf8::substr_chars(s, 1, 3);  // "éll"
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, std::string{"éll"});
}
