#include <gtest/gtest.h>
#include <server/web_socket/ws_session.h>

#include <string_view>

TEST(web_socket, test_ws_key) {
  using namespace server::web_socket;

  EXPECT_EQ(ws_session::get_web_socket_key_result("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}
