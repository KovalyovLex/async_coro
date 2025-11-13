
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>
#include <server/web_socket/request_frame.h>

#include <cstddef>
#include <cstring>
#include <memory>

namespace server::web_socket {

async_coro::task<expected<void, std::string>> request_frame::read_payload(socket_layer::connection& conn, std::span<std::byte> rest_data_in_buffer) {
  using result_t = expected<void, std::string>;

  if (payload_length == 0) {
    co_return result_t{};
  }

  payload = std::make_unique<std::byte[]>(payload_length);  // NOLINT(*c-array*)

  std::span data{payload.get(), payload_length};

  if (rest_data_in_buffer.size() > 0) {
    const auto n_copy = std::min(payload_length, rest_data_in_buffer.size());
    std::memcpy(data.data(), rest_data_in_buffer.data(), n_copy);
    data = data.subspan(n_copy);
  }

  while (!data.empty()) {
    // read more data from connection
    auto res = co_await conn.read_buffer(data);
    if (!res) {
      co_return result_t{unexpect, std::move(res).error()};
    }
    data = data.subspan(res.value());
  }

  if (mask) {
    auto& msk = *mask;

    for (size_t i = 0; i < payload_length; i++) {
      payload[i] ^= msk[i % 4];  // NOLINT(*array-index*)
    }
  }

  co_return result_t{};
}

}  // namespace server::web_socket
