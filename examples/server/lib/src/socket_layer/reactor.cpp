#include <server/socket_layer/reactor.h>

namespace server::socket_layer {

reactor::reactor() noexcept = default;

reactor::~reactor() noexcept = default;

void reactor::process_loop(std::chrono::nanoseconds max_wait) {
  _reactor.process_loop(max_wait);
}

size_t reactor::add_connection(connection_id conn) {
  return _reactor.add_sock(conn.get_platform_id());
}

void reactor::close_connection(connection_id conn, size_t index) {
  _reactor.remove_sock(conn.get_platform_id(), index);
}

void reactor::continue_after_receive_data(connection_id conn, size_t index, continue_callback_t&& callback) {
  _reactor.continue_after_receive_data(conn.get_platform_id(), index, std::move(callback));
}

void reactor::continue_after_sent_data(connection_id conn, size_t index, continue_callback_t&& callback) {
  _reactor.continue_after_sent_data(conn.get_platform_id(), index, std::move(callback));
}

}  // namespace server::socket_layer
