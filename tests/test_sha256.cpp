// Tests: tts::crypto::Sha256 — NIST test vectors.
#include <gtest/gtest.h>
#include "tts/sha256.hpp"
#include <array>
#include <string>

using namespace tts::crypto;

TEST(Sha256, EmptyString) {
    auto h = sha256_hex("");
    EXPECT_EQ(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256, Abc) {
    auto h = sha256_hex("abc");
    EXPECT_EQ(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256, LongMessage) {
    // "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    auto h = sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    EXPECT_EQ(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256, StreamingMatchesOneShot) {
    const std::string msg(1000, 'x');
    Sha256 s1; s1.update(msg);
    auto a = to_hex(s1.finalize());

    Sha256 s2;
    for (std::size_t i = 0; i < msg.size(); i += 7) {
        s2.update(std::string_view{msg.data() + i, std::min<std::size_t>(7, msg.size() - i)});
    }
    auto b = to_hex(s2.finalize());
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, sha256_hex(msg));
}

TEST(Sha256, LargeMessage) {
    // 1,000,000 'a' chars — NIST test vector.
    const std::string msg(1'000'000, 'a');
    auto h = sha256_hex(msg);
    EXPECT_EQ(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
