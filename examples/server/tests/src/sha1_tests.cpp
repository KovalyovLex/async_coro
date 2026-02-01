#include <async_coro/warnings.h>
#include <gtest/gtest.h>
#include <server/utils/sha1.h>

#if SERVER_ENABLE_OPENSSL
#include <openssl/sha.h>
#endif

#include <cstring>
#include <string_view>

/*
 * The 3 test vectors from FIPS PUB 180-1
 */
TEST(sha1_hash, test_standard) {
  // https://www.di-mgt.com.au/sha_testvectors.html
  // https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/SHA1.pdf

  {
    server::sha1_hash checksum;
    checksum.update("abc");
    EXPECT_EQ(checksum.get_value_str(), "a9993e364706816aba3e25717850c26c9cd0d89d") << "test 'abc'";
  }

  {
    server::sha1_hash checksum;
    checksum.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    EXPECT_EQ(checksum.get_value_str(), "84983e441c3bd26ebaae4aa1f95129e5e54670f1") << "test 'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'";
  }

  {
    server::sha1_hash checksum;
    checksum.update("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu");
    EXPECT_EQ(checksum.get_value_str(), "a49b2446a02c645bf419f995b67091253a04a259") << "test 'abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu'";
  }

  {
    server::sha1_hash checksum;
    for (int i = 0; i < 1000000 / 200; ++i) {
      checksum.update(
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    EXPECT_EQ(checksum.get_value_str(), "34aa973cd4c4daa4f61eeb2bdbad27316534016f") << "test A million repetitions of 'a'";
  }

  // https://en.wikipedia.org/wiki/SHA-1
  {
    server::sha1_hash checksum;
    checksum.update("The quick brown fox jumps over the lazy dog");
    EXPECT_EQ(checksum.get_value_str(), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12") << "test 'The quick brown fox jumps over the lazy dog'";
  }

  {
    server::sha1_hash checksum;
    checksum.update("The quick brown fox jumps over the lazy cog");
    EXPECT_EQ(checksum.get_value_str(), "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }

  {
    static constexpr auto hash_bytes = server::sha1_hash{"The quick brown fox jumps over the lazy cog"}.get_value();
    constexpr auto hash_str = std::string_view{hash_bytes.data(), hash_bytes.size()};
    EXPECT_EQ(hash_str, "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }
}

TEST(sha1_hash, test_str_conversions) {
  constexpr std::string_view data_str{"The quick brown fox jumps over the lazy cog"};

  {
    server::sha1_hash checksum;
    checksum.update(data_str);
    EXPECT_EQ(checksum.get_value_str(), "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }

  {
    server::sha1_hash checksum;
    checksum.update(data_str);
    auto array = checksum.get_value();
    std::string_view data_view{array.data(), array.size()};
    EXPECT_EQ(data_view, "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }

  {
    server::sha1_hash checksum;
    checksum.update(data_str);
    auto array = checksum.get_bytes();
    EXPECT_EQ(server::sha1_hash::convert_digest_to_value_str(array), "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }

  {
    server::sha1_hash checksum;
    checksum.update(data_str);
    auto digest = checksum.get_bytes();
    auto array = server::sha1_hash::convert_digest_to_value(digest);
    std::string_view data_view{array.data(), array.size()};
    EXPECT_EQ(data_view, "de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3") << "test 'The quick brown fox jumps over the lazy cog'";
  }
}

TEST(sha1_hash, test_slow) {
  // https://www.di-mgt.com.au/sha_testvectors.html

  server::sha1_hash checksum;
  constexpr std::string_view data_str{"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"};

  for (int i = 0; i < 16777216; ++i) {
    checksum.update(data_str);
  }
  EXPECT_EQ(checksum.get_value_str(), "7789f0c9ef7bfc40d93311143dfbe69e2017f592") << "test 16,777,216 repititions of abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno";
}

#if SERVER_ENABLE_OPENSSL
ASYNC_CORO_WARNINGS_PUSH
ASYNC_CORO_WARNINGS_CLANG_IGNORE("deprecated-declarations")
ASYNC_CORO_WARNINGS_GCC_IGNORE("deprecated-declarations")
ASYNC_CORO_WARNINGS_MSVC_IGNORE(4996)

TEST(sha1_hash, test_slow_open_ssl) {
  // https://www.di-mgt.com.au/sha_testvectors.html

  SHA_CTX checksum;
  SHA1_Init(&checksum);

  constexpr std::string_view data_str{"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"};
  for (int i = 0; i < 16777216; ++i) {
    SHA1_Update(&checksum, data_str.data(), data_str.size());
  }
  std::array<unsigned char, SHA_DIGEST_LENGTH> res_array;  // NOLINT(*init*)
  SHA1_Final(res_array.data(), &checksum);

  server::sha1_hash::bytes_digest_buffer_t buf;
  static_assert(res_array.size() == buf.size());

  std::memcpy(buf.data(), res_array.data(), res_array.size());

  EXPECT_EQ(server::sha1_hash::convert_digest_to_value_str(buf), "7789f0c9ef7bfc40d93311143dfbe69e2017f592") << "test 16,777,216 repititions of abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno";
}

ASYNC_CORO_WARNINGS_POP
#endif

/*
 * Other tests
 */
TEST(sha1_hash, test_other) {
  {
    server::sha1_hash checksum;
    EXPECT_EQ(checksum.get_value_str(), "da39a3ee5e6b4b0d3255bfef95601890afd80709") << "test No string";
  }

  {
    server::sha1_hash checksum;
    checksum.update("");
    EXPECT_EQ(checksum.get_value_str(), "da39a3ee5e6b4b0d3255bfef95601890afd80709") << "test Empty string";
  }

  {
    server::sha1_hash checksum;
    checksum.update("abcde");
    EXPECT_EQ(checksum.get_value_str(), "03de6c570bfe24bfc328ccd7ca46b76eadaf4334") << "test 'abcde'";
  }
}
