// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/sha256.hpp"

#include <string>

#include "test_support.hpp"

using namespace rs;

RS_TEST(known_answer_vectors) {
    // FIPS 180-4 / NIST published vectors.
    RS_CHECK_EQ(
        hash::sha256_hex(""),
        std::string(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    RS_CHECK_EQ(
        hash::sha256_hex("abc"),
        std::string(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    RS_CHECK_EQ(
        hash::sha256_hex(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string(
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

RS_TEST(handles_multi_block_input) {
    std::string million_a(1000000, 'a');
    RS_CHECK_EQ(
        hash::sha256_hex(million_a),
        std::string(
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

RS_TEST(incremental_update_matches_single_shot) {
    hash::Sha256 incremental;
    incremental.update("abcdbcdecdefdefgefghfghi");
    incremental.update("jhijkijkljklmklmnlmnomnopnopq");
    RS_CHECK_EQ(incremental.hex_digest(),
                hash::sha256_hex(
                    "abcdbcdecdefdefgefghfghijhijkijkljklmklmnlmnomnopnopq"));
}

RS_TEST(length_boundaries_around_a_block) {
    // 55, 56, 63, 64 and 65 bytes exercise every padding branch.
    for (std::size_t n : {std::size_t{55}, std::size_t{56}, std::size_t{63},
                          std::size_t{64}, std::size_t{65}}) {
        const std::string input(n, 'x');
        hash::Sha256 a;
        a.update(input);
        hash::Sha256 b;
        for (char c : input) b.update(&c, 1);
        RS_CHECK_EQ(a.hex_digest(), b.hex_digest());
    }
}

RS_TEST(uri_form_is_prefixed) {
    RS_CHECK_EQ(hash::sha256_uri("abc").substr(0, 7), std::string("sha256:"));
    RS_CHECK_EQ(hash::sha256_uri("abc").size(), std::size_t{7 + 64});
}

RS_TEST_MAIN("sha256")
