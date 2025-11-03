
#include <gtest/gtest.h>
#include <server/http1/http_error.h>
#include <server/http1/request.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace server::http1;  // NOLINT(*-using-namespace)

static std::vector<std::byte> to_bytes(std::string_view str) {
  std::vector<std::byte> out;
  out.reserve(str.size());
  for (char chr : str) {
    out.push_back(static_cast<std::byte>(chr));
  }
  return out;
}

static std::span<const std::byte> to_span(const std::vector<std::byte>& v) {
  return std::span<const std::byte>{v.data(), v.size()};
}

TEST(request_parser, simple_get_single_portion) {
  request req;
  request::parser_ptr parser{};

  req.begin_parse(parser, {});

  const auto bytes = to_bytes("GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n");
  auto res = req.parse_data_part(parser, to_span(bytes));

  ASSERT_TRUE(res);
  EXPECT_TRUE(req.is_parsed());
  EXPECT_EQ(req.get_target(), "/index.html");

  const auto* hdr = req.find_header("Host");
  ASSERT_NE(hdr, nullptr);
  EXPECT_EQ(hdr->second, "example.com");
}

TEST(request_parser, wrong_method) {
  request req;
  request::parser_ptr parser{};

  req.begin_parse(parser, {});

  const auto bytes = to_bytes("BADMETHOD / HTTP/1.1\r\n\r\n");
  auto res = req.parse_data_part(parser, to_span(bytes));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::BadRequest);
  EXPECT_FALSE(req.is_parsed());
}

TEST(request_parser, missing_version) {
  request req;
  request::parser_ptr parser{};

  req.begin_parse(parser, {});

  const auto bytes = to_bytes("GET /nover\r\nHost: a\r\n\r\n");
  auto res = req.parse_data_part(parser, to_span(bytes));

  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::BadRequest);
  EXPECT_FALSE(req.is_parsed());
}

// Helper: feed string in multiple arbitrary parts to simulate network fragmentation
static server::expected<void, http_error> feed_parts(request& req, request::parser_ptr& parser, std::string_view full, const std::vector<size_t>& cuts) {
  size_t pos = 0;
  for (size_t cut : cuts) {
    size_t len = std::min(cut, full.size() - pos);
    auto part = full.substr(pos, len);
    auto bytes = to_bytes(part);
    auto res = req.parse_data_part(parser, to_span(bytes));
    if (!res) {
      return res;
    }
    pos += len;
    if (pos >= full.size()) {
      break;
    }
  }

  // if anything remains
  if (pos < full.size()) {
    auto bytes = to_bytes(full.substr(pos));
    return req.parse_data_part(parser, to_span(bytes));
  }

  return server::expected<void, http_error>{};
}

TEST(request_parser, chunked_various_splits) {
  // Body: "Wikipedia" in chunked form
  const std::string_view total =
      "POST /chunk HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "4\r\nWiki\r\n"
      "5\r\npedia\r\n"
      "0\r\n\r\n";

  // try a few different cut patterns (simulate network partials)
  std::vector<std::vector<size_t>> patterns = {
      {10, 30, 10, 100},       // several moderate chunks
      {1, 1, 1, 1, 1, 1000},   // byte-by-byte then rest
      {25, 2, 2, 2, 2, 1000},  // break inside chunk size and data
  };

  for (const auto& cuts : patterns) {
    request req;
    request::parser_ptr parser{};
    req.begin_parse(parser, {});

    auto res = feed_parts(req, parser, total, cuts);
    ASSERT_TRUE(res) << (res ? "ok" : std::string{res.error().get_reason()});
    EXPECT_TRUE(req.is_parsed());
    EXPECT_EQ(std::string(req.get_body()), "Wikipedia");
  }
}

TEST(request_parser, chunked_invalid_chunk_size) {
  const std::string total =
      "POST /chunk HTTP/1.1\r\n"
      "Transfer-Encoding: chunked\r\n"
      "\r\n"
      "g\r\nbad\r\n"
      "0\r\n\r\n";

  request req;
  request::parser_ptr parser{};
  req.begin_parse(parser, {});

  auto res = req.parse_data_part(parser, to_span(to_bytes(total)));
  ASSERT_FALSE(res);
  EXPECT_EQ(res.error().status_code, status_code::BadRequest);
}
