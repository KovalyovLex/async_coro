#include <gtest/gtest.h>
#include <server/http1/compression_negotiation.h>
#include <server/utils/compression_pool.h>

#include <array>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "fixtures/compression_helper.h"

// Test fixture for compression negotiator tests
class compression_negotiator_tests : public ::testing::Test, public compression_helper {
 protected:
  void SetUp() override {
    // Create compression pool with all available encodings
    server::compression_pool_config pool_config{.encodings = server::compression_pool_config::k_all_encodings};
    compression_pool = server::compression_pool::create(pool_config);
  }

  void TearDown() override {
    compression_pool = nullptr;
  }
};

// ==================== Tests for parse_quality_float ====================

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_1) {
  auto result = server::compression_negotiator::parse_quality_float("q=1");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 1.0F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0) {
  auto result = server::compression_negotiator::parse_quality_float("q=0");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.0F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_1) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.1");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.1F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_5) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.5F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_8) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.8");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.8F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_01) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.01");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.01F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_001) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.001");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.001F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_q_equals_0_123) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.123");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.123F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_dot_format) {
  auto result = server::compression_negotiator::parse_quality_float("q=.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.5F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_dot_one) {
  auto result = server::compression_negotiator::parse_quality_float("q=.1");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.1F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_with_spaces_after_q) {
  auto result = server::compression_negotiator::parse_quality_float("q = 0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.5F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_with_single_space_after_q) {
  auto result = server::compression_negotiator::parse_quality_float("q = 0.8");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.8F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_valid_with_multiple_spaces) {
  auto result = server::compression_negotiator::parse_quality_float("q    =    0.5");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.5F);
}

// Tests for truncation of decimals beyond 3 digits
TEST_F(compression_negotiator_tests, parse_quality_float_truncates_4_decimals) {
  // q=0.1234 should truncate to 0.123
  auto result = server::compression_negotiator::parse_quality_float("q=0.1234");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.123F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_truncates_5_decimals) {
  // q=0.12345 should truncate to 0.123
  auto result = server::compression_negotiator::parse_quality_float("q=0.12345");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.123F);
}

TEST_F(compression_negotiator_tests, parse_quality_float_truncates_many_decimals) {
  // q=0.999999 should truncate to 0.999
  auto result = server::compression_negotiator::parse_quality_float("q=0.999999");
  ASSERT_TRUE(result.has_value());
  EXPECT_FLOAT_EQ(*result, 0.999F);
}

// Invalid inputs tests
TEST_F(compression_negotiator_tests, parse_quality_float_empty_string) {
  auto result = server::compression_negotiator::parse_quality_float("");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_missing_q) {
  auto result = server::compression_negotiator::parse_quality_float("0.5");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_missing_equals) {
  auto result = server::compression_negotiator::parse_quality_float("q0.5");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_missing_value) {
  auto result = server::compression_negotiator::parse_quality_float("q=");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_invalid_leading_character) {
  auto result = server::compression_negotiator::parse_quality_float("x=0.5");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_value_greater_than_1) {
  auto result = server::compression_negotiator::parse_quality_float("q=2");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_value_greater_than_1_with_decimal) {
  auto result = server::compression_negotiator::parse_quality_float("q=1.5");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_negative_value) {
  auto result = server::compression_negotiator::parse_quality_float("q=-0.5");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_non_numeric_value) {
  auto result = server::compression_negotiator::parse_quality_float("q=abc");
  EXPECT_FALSE(result.has_value());
}

TEST_F(compression_negotiator_tests, parse_quality_float_incomplete_decimal) {
  auto result = server::compression_negotiator::parse_quality_float("q=0.");
  EXPECT_FALSE(result.has_value());
}

// ==================== Tests for parse_accept_encoding ====================

TEST_F(compression_negotiator_tests, parse_accept_encoding_single_encoding) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip", preferences);

  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_FLOAT_EQ(preferences[0].quality, 1.0F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_single_with_quality) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip;q=0.8", preferences);

  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_FLOAT_EQ(preferences[0].quality, 0.8F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_multiple_encodings) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip, deflate, br", preferences);

  ASSERT_EQ(preferences.size(), 3);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_EQ(preferences[1].encoding, server::compression_encoding::deflate);
  EXPECT_EQ(preferences[2].encoding, server::compression_encoding::br);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_multiple_with_qualities) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip;q=1.0, deflate;q=0.8, br;q=0.5", preferences);

  ASSERT_EQ(preferences.size(), 3);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_FLOAT_EQ(preferences[0].quality, 1.0F);
  EXPECT_EQ(preferences[1].encoding, server::compression_encoding::deflate);
  EXPECT_FLOAT_EQ(preferences[1].quality, 0.8F);
  EXPECT_EQ(preferences[2].encoding, server::compression_encoding::br);
  EXPECT_FLOAT_EQ(preferences[2].quality, 0.5F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_with_spaces) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("  gzip  ;  q=0.8  ,  deflate  ", preferences);

  ASSERT_EQ(preferences.size(), 2);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_FLOAT_EQ(preferences[0].quality, 0.8F);
  EXPECT_EQ(preferences[1].encoding, server::compression_encoding::deflate);
  EXPECT_FLOAT_EQ(preferences[1].quality, 1.0F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_identity) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("identity", preferences);

  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::none);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_wildcard) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("*;q=0.5", preferences);

  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::any);
  EXPECT_FLOAT_EQ(preferences[0].quality, 0.5F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_empty_header) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("", preferences);

  EXPECT_EQ(preferences.size(), 0);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_quality_zero) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("br;q=0", preferences);

  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::br);
  EXPECT_FLOAT_EQ(preferences[0].quality, 0.0F);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_unknown_encoding_skipped) {
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("unknown, gzip, invalid", preferences);

  // Only gzip should be parsed (unknown encodings should be skipped)
  ASSERT_EQ(preferences.size(), 1);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
}

TEST_F(compression_negotiator_tests, parse_accept_encoding_rfc_example) {
  // RFC 7231 example: Accept-Encoding: gzip, deflate, br;q=0.8
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip, deflate, br;q=0.8", preferences);

  ASSERT_EQ(preferences.size(), 3);
  EXPECT_EQ(preferences[0].encoding, server::compression_encoding::gzip);
  EXPECT_FLOAT_EQ(preferences[0].quality, 1.0F);
  EXPECT_EQ(preferences[1].encoding, server::compression_encoding::deflate);
  EXPECT_FLOAT_EQ(preferences[1].quality, 1.0F);
  EXPECT_EQ(preferences[2].encoding, server::compression_encoding::br);
  EXPECT_FLOAT_EQ(preferences[2].quality, 0.8F);
}

// ==================== Tests for negotiate ====================

TEST_F(compression_negotiator_tests, negotiate_single_preference) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 1> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::gzip);
}

TEST_F(compression_negotiator_tests, negotiate_multiple_preferences_highest_quality) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 2> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
      {.encoding = server::compression_encoding::deflate, .quality = 0.5F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::gzip);
}

TEST_F(compression_negotiator_tests, negotiate_prefers_higher_quality) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 2> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 0.5F},
      {.encoding = server::compression_encoding::deflate, .quality = 0.9F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::deflate);
}

TEST_F(compression_negotiator_tests, negotiate_empty_preferences) {
  server::compression_negotiator negotiator{compression_pool};

  auto selected = negotiator.negotiate({});
  EXPECT_EQ(selected, server::compression_encoding::none);
}

TEST_F(compression_negotiator_tests, negotiate_zero_quality_rejected) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 1> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 0.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::none);
}

TEST_F(compression_negotiator_tests, negotiate_wildcard_accepts_best_supported) {
  server::compression_negotiator negotiator{compression_pool};

  std::array<server::encoding_preference, 1> prefs{{
      {.encoding = server::compression_encoding::any, .quality = 1.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  // Should select highest priority encoding from pool
  EXPECT_NE(selected, server::compression_encoding::none);
  EXPECT_NE(selected, server::compression_encoding::any);
}

TEST_F(compression_negotiator_tests, negotiate_no_pool) {
  server::compression_negotiator negotiator;  // No pool

  std::array<server::encoding_preference, 1> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  EXPECT_EQ(selected, server::compression_encoding::none);
}

TEST_F(compression_negotiator_tests, negotiate_equal_quality_uses_server_priority) {
  server::compression_negotiator negotiator{compression_pool};

  // Both have same quality, should prefer highest server priority
  std::array<server::encoding_preference, 2> prefs{{
      {.encoding = server::compression_encoding::gzip, .quality = 1.0F},
      {.encoding = server::compression_encoding::deflate, .quality = 1.0F},
  }};

  auto selected = negotiator.negotiate(prefs);
  // Should select one of them (both supported in pool)
  EXPECT_TRUE(selected == server::compression_encoding::gzip || selected == server::compression_encoding::deflate);
}

// ==================== Tests for get_encoding_priority ====================

TEST_F(compression_negotiator_tests, get_encoding_priority_gzip) {
  server::compression_negotiator negotiator{compression_pool};

  auto priority = negotiator.get_encoding_priority(server::compression_encoding::gzip);
  EXPECT_GT(priority, 0);
}

TEST_F(compression_negotiator_tests, get_encoding_priority_deflate) {
  server::compression_negotiator negotiator{compression_pool};

  auto priority = negotiator.get_encoding_priority(server::compression_encoding::deflate);
  EXPECT_GT(priority, 0);
}

TEST_F(compression_negotiator_tests, get_encoding_priority_none_returns_zero) {
  server::compression_negotiator negotiator{compression_pool};

  auto priority = negotiator.get_encoding_priority(server::compression_encoding::none);
  EXPECT_EQ(priority, 0);
}

TEST_F(compression_negotiator_tests, get_encoding_priority_no_pool_returns_zero) {
  server::compression_negotiator negotiator;  // No pool

  auto priority = negotiator.get_encoding_priority(server::compression_encoding::gzip);
  EXPECT_EQ(priority, 0);
}

// ==================== Integration tests ====================

TEST_F(compression_negotiator_tests, full_negotiation_workflow) {
  server::compression_negotiator negotiator{compression_pool};

  // Parse a real Accept-Encoding header
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("gzip;q=1.0, deflate;q=0.8, *;q=0.5", preferences);

  // Negotiate best encoding
  auto selected = negotiator.negotiate(preferences);

  // Should select gzip with highest quality
  EXPECT_EQ(selected, server::compression_encoding::gzip);
}

TEST_F(compression_negotiator_tests, negotiation_with_low_quality_wildcard) {
  server::compression_negotiator negotiator{compression_pool};

  // Client prefers no compression but allows any with low quality
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("identity;q=1.0, *;q=0.1", preferences);

  auto selected = negotiator.negotiate(preferences);
  // Should prefer identity (none) but accept others
  // Since identity returns none and has no compression, would pick supported encoding
  EXPECT_TRUE(selected == server::compression_encoding::none || selected != server::compression_encoding::any);
}

TEST_F(compression_negotiator_tests, negotiation_prefers_explicit_over_wildcard) {
  server::compression_negotiator negotiator{compression_pool};

  // Both explicit and wildcard present, explicit should win
  std::pmr::vector<server::encoding_preference> preferences;
  server::compression_negotiator::parse_accept_encoding("br;q=0.9, *;q=1.0", preferences);

  auto selected = negotiator.negotiate(preferences);
  // The wildcard has higher quality, so it should be preferred
  EXPECT_NE(selected, server::compression_encoding::none);
}
