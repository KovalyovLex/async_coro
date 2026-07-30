// Fake connection used to capture outgoing bytes
#pragma once

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <server/core/i_write_connection.h>
#include <server/utils/expected.h>

#include <span>

struct test_write_connection : server::core::i_write_connection {
  std::string sent;

  async_coro::task<server::expected<void, std::string>> write_buffer(std::span<const std::byte> bytes) override {
    sent.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    co_return server::expected<void, std::string>{};
  }

  [[nodiscard]] bool is_closed() const noexcept override { return false; }

  void close_connection() override {}

  template <class TReq>
  static std::string serialize(TReq& req) {
    async_coro::scheduler scheduler{std::make_unique<async_coro::execution_system>(async_coro::execution_system_config{})};

    test_write_connection conn;

    auto handle = scheduler.start_task(req.send(conn), async_coro::execution_queues::main);
    EXPECT_TRUE(handle.done());

    return std::move(conn.sent);
  }
};
