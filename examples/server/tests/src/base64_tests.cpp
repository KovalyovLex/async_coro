#include <gtest/gtest.h>
#include <server/utils/base64.h>

#include <string_view>

TEST(base64, test_simple) {
  using namespace server;

  EXPECT_EQ(base64_encode(""), "");
  EXPECT_EQ(base64_encode("f"), "Zg==");
  EXPECT_EQ(base64_encode("fo"), "Zm8=");
  EXPECT_EQ(base64_encode("foo"), "Zm9v");
  EXPECT_EQ(base64_encode("foob"), "Zm9vYg==");
  EXPECT_EQ(base64_encode("fooba"), "Zm9vYmE=");
  EXPECT_EQ(base64_encode("foobar"), "Zm9vYmFy");
  EXPECT_EQ(base64_encode("Many hands make light work."), "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu");
  EXPECT_EQ(base64_encode("<=>h"), "PD0+aA==");
  EXPECT_EQ(base64_encode("<?�"), "PD/vv70=");
}

TEST(base64, test_simple_url) {
  using namespace server;

  EXPECT_EQ(base64_url_encode(""), "");
  EXPECT_EQ(base64_url_encode("f"), "Zg");
  EXPECT_EQ(base64_url_encode("fo"), "Zm8");
  EXPECT_EQ(base64_url_encode("foo"), "Zm9v");
  EXPECT_EQ(base64_url_encode("foob"), "Zm9vYg");
  EXPECT_EQ(base64_url_encode("fooba"), "Zm9vYmE");
  EXPECT_EQ(base64_url_encode("foobar"), "Zm9vYmFy");
  EXPECT_EQ(base64_url_encode("Many hands make light work."), "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu");
  EXPECT_EQ(base64_url_encode("<=>h"), "PD0-aA");
  EXPECT_EQ(base64_url_encode("<?�"), "PD_vv70");
}

TEST(base64, test_simple_reverse) {
  using namespace server;

  EXPECT_EQ(base64_decode_str(base64_encode("")), "");
  EXPECT_EQ(base64_decode_str(base64_encode("f")), "f");
  EXPECT_EQ(base64_decode_str(base64_encode("fo")), "fo");
  EXPECT_EQ(base64_decode_str(base64_encode("foo")), "foo");
  EXPECT_EQ(base64_decode_str(base64_encode("foob")), "foob");
  EXPECT_EQ(base64_decode_str(base64_encode("fooba")), "fooba");
  EXPECT_EQ(base64_decode_str(base64_encode("foobar")), "foobar");
  EXPECT_EQ(base64_decode_str(base64_encode("Many hands make light work.")), "Many hands make light work.");
  EXPECT_EQ(base64_decode_str(base64_encode("<=>h")), "<=>h");
  EXPECT_EQ(base64_decode_str(base64_encode("<?�")), "<?�");
}

TEST(base64, test_strict_base64) {
  using namespace server;

  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("Zm9v"), "foo");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("PD0+aA=="), "<=>h");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("PD0+aA="), "<=>h");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("PD0+aA"), "<=>h");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("PD0-aA"), "");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64};
    EXPECT_EQ(dec.decode_str("PD0_aA"), "");
  }
}

TEST(base64, test_strict_base64_url) {
  using namespace server;

  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64_url};
    EXPECT_EQ(dec.decode_str("Zm9v"), "foo");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64_url};
    EXPECT_EQ(dec.decode_str("PD0-aA"), "<=>h");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64_url};
    EXPECT_EQ(dec.decode_str("PD0+aA"), "");
  }
  {
    base64_decoder dec{base64_decoder::decode_policy::strict_base64_url};
    EXPECT_EQ(dec.decode_str("PD0/aA"), "");
  }
}

TEST(base64, test_constexpr_simple) {
  using namespace server;

  constexpr auto str1 = ""_base64;
  constexpr auto str2 = "f"_base64;
  constexpr auto str3 = "fo"_base64;
  constexpr auto str4 = "foo"_base64;
  constexpr auto str5 = "foob"_base64;
  constexpr auto str6 = "fooba"_base64;
  constexpr auto str7 = "foobar"_base64;
  constexpr auto str8 = "Many hands make light work."_base64;
  constexpr auto str9 = "<=>h"_base64;
  constexpr auto str10 = "<?�"_base64;

  EXPECT_EQ(str1.get_string_view(), "");
  EXPECT_EQ(str2.get_string_view(), "Zg==");
  EXPECT_EQ(str3.get_string_view(), "Zm8=");
  EXPECT_EQ(str4.get_string_view(), "Zm9v");
  EXPECT_EQ(str5.get_string_view(), "Zm9vYg==");
  EXPECT_EQ(str6.get_string_view(), "Zm9vYmE=");
  EXPECT_EQ(str7.get_string_view(), "Zm9vYmFy");
  EXPECT_EQ(str8.get_string_view(), "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu");
  EXPECT_EQ(str9.get_string_view(), "PD0+aA==");
  EXPECT_EQ(str10.get_string_view(), "PD/vv70=");
}

TEST(base64, test_constexpr_simple_url) {
  using namespace server;

  constexpr auto str1 = ""_base64_url;
  constexpr auto str2 = "f"_base64_url;
  constexpr auto str3 = "fo"_base64_url;
  constexpr auto str4 = "foo"_base64_url;
  constexpr auto str5 = "foob"_base64_url;
  constexpr auto str6 = "fooba"_base64_url;
  constexpr auto str7 = "foobar"_base64_url;
  constexpr auto str8 = "Many hands make light work."_base64_url;
  constexpr auto str9 = "<=>h"_base64_url;
  constexpr auto str10 = "<?�"_base64_url;

  EXPECT_EQ(str1.get_string_view(), "");
  EXPECT_EQ(str2.get_string_view(), "Zg");
  EXPECT_EQ(str3.get_string_view(), "Zm8");
  EXPECT_EQ(str4.get_string_view(), "Zm9v");
  EXPECT_EQ(str5.get_string_view(), "Zm9vYg");
  EXPECT_EQ(str6.get_string_view(), "Zm9vYmE");
  EXPECT_EQ(str7.get_string_view(), "Zm9vYmFy");
  EXPECT_EQ(str8.get_string_view(), "TWFueSBoYW5kcyBtYWtlIGxpZ2h0IHdvcmsu");
  EXPECT_EQ(str9.get_string_view(), "PD0-aA");
  EXPECT_EQ(str10.get_string_view(), "PD_vv70");
}
