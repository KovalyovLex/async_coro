

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <server/tcp_server.h>
#include <server/tcp_server_config.h>

#include <atomic>
#include <csignal>
#include <memory>
#include <string_view>

std::atomic<server::tcp_server*> global_server;  // NOLINT(*-non-const-*)

void signal_handler(int signal) {
  if (auto* serv = global_server.load(std::memory_order::acquire)) {
    serv->terminate();
  }
}

int main(int argc, char** argv) {
  int port = 8080;                          // NOLINT
  if (argc > 1) port = std::atoi(argv[1]);  // NOLINT

  server::tcp_server serv;

  global_server.store(std::addressof(serv), std::memory_order::relaxed);

  server::tcp_server_config conf{
      .port = static_cast<uint16_t>(port),
  };

  async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(
      async_coro::execution_system_config{.worker_configs = {{"worker1"},
                                                             {"worker2"}}})};

  std::signal(SIGINT, signal_handler);

  serv.serve(conf, {}, [&scheduler](auto conn) mutable {
    scheduler.start_task([conn = std::move(conn)]() mutable {
      constexpr std::string_view resp = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nHello";

      return conn.write_buffer(as_bytes(std::span{resp.data(), resp.length()}));
    });
  });

  global_server.store(nullptr, std::memory_order::relaxed);

  return 0;
}
