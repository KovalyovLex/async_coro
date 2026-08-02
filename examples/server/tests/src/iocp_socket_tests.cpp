#if WIN_IOCP_ENABLED

#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/task.h>
#include <gtest/gtest.h>
#include <server/io/iocp_listener.h>
#include <server/io/iocp_reactor.h>
#include <server/io/iocp_socket.h>
#include <server/io/winsock_init.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Windows socket headers
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "utils/io_helpers.h"
#include "utils/temp_file.h"

// ============================================================================
// Winsock initialization — single lazy call, reused by reactor and raw socket APIs.
// ============================================================================

TEST(iocp_socket_tests, socket_basic_lifecycle) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);
    EXPECT_FALSE(client_socket.is_closed());
    EXPECT_NE(client_socket.get_native_handle(), server::io::invalid_socket_id);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);
    EXPECT_FALSE(server_socket.is_closed());
    EXPECT_NE(server_socket.get_native_handle(), server::io::invalid_socket_id);

    // Close both sockets
    auto client_close = client_socket.close();
    if (!client_close) {
      EXPECT_TRUE(client_close) << client_close.error();
      co_return -1;
    }
    EXPECT_TRUE(client_socket.is_closed());

    auto server_close = server_socket.close();
    if (!server_close) {
      EXPECT_TRUE(server_close) << server_close.error();
      co_return -1;
    }
    EXPECT_TRUE(server_socket.is_closed());

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_send_receive_small) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Client sends "Hello, IOCP!"
    constexpr std::string_view message = "Hello, IOCP!";
    auto send_result = co_await client_socket.send(
        std::as_bytes(std::span(message)));
    if (!send_result) {
      EXPECT_TRUE(send_result) << send_result.error();
      co_return -1;
    }
    EXPECT_EQ(send_result.value(), message.size());

    // Server receives and verifies content
    std::array<std::byte, 64> recv_buffer{};
    auto recv_result = co_await server_socket.receive(recv_buffer);
    if (!recv_result) {
      EXPECT_TRUE(recv_result) << recv_result.error();
      co_return -1;
    }
    EXPECT_EQ(recv_result.value(), message.size());

    std::string_view received_data(reinterpret_cast<const char*>(recv_buffer.data()), recv_result.value());
    EXPECT_EQ(received_data, message);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_send_receive_large) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  constexpr size_t data_size = 65536;  // 64 KB

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Generate test data
    std::string send_data = test_utils::generate_test_data(data_size, 42);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Send large data via client socket
    auto send_result = co_await client_socket.send(
        std::as_bytes(std::span<const char>(send_data)));
    if (!send_result) {
      EXPECT_TRUE(send_result) << send_result.error();
      co_return -1;
    }
    EXPECT_EQ(send_result.value(), data_size);

    // Receive via server socket
    std::vector<std::byte> recv_buffer(data_size);
    auto recv_result = co_await server_socket.receive(recv_buffer);
    if (!recv_result) {
      EXPECT_TRUE(recv_result) << recv_result.error();
      co_return -1;
    }
    EXPECT_EQ(recv_result.value(), data_size);

    // Verify all bytes match
    std::string received_data(recv_result.value(), '\0');
    for (size_t i = 0; i < recv_result.value(); ++i) {
      received_data[i] = static_cast<char>(recv_buffer[i]);
    }
    EXPECT_EQ(received_data, send_data);

    // Clean up
    (void)client_socket.close();
    (void)server_socket.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_multiple_messages) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Send multiple messages in sequence
    constexpr std::array<std::string_view, 3> messages = {"msg1", "msg2", "msg3"};

    for (const auto& msg : messages) {
      auto send_result = co_await client_socket.send(std::as_bytes(std::span<const char>(msg)));
      if (!send_result) {
        EXPECT_TRUE(send_result) << send_result.error();
        co_return -1;
      }
      EXPECT_EQ(send_result.value(), msg.size());
    }

    // Receive and verify each message
    for (const auto& expected_msg : messages) {
      std::array<std::byte, 64> recv_buffer{};
      auto recv_result = co_await server_socket.receive(recv_buffer);
      if (!recv_result) {
        EXPECT_TRUE(recv_result) << recv_result.error();
        co_return -1;
      }
      EXPECT_EQ(recv_result.value(), expected_msg.size());

      std::string received_data(recv_result.value(), '\0');
      for (size_t i = 0; i < recv_result.value(); ++i) {
        received_data[i] = static_cast<char>(recv_buffer[i]);
      }
      EXPECT_EQ(received_data, expected_msg);
    }

    // Clean up
    (void)client_socket.close();
    (void)server_socket.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_partial_send) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  constexpr size_t data_size = 32768;  // 32 KB — large enough to potentially be split

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Generate test data with a different index to distinguish from other tests
    std::string send_data = test_utils::generate_test_data(data_size, 7);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Send a large buffer — send() loops internally until all data is sent
    auto send_result = co_await client_socket.send(std::as_bytes(std::span<const char>(send_data)));
    if (!send_result) {
      EXPECT_TRUE(send_result) << send_result.error();
      co_return -1;
    }
    EXPECT_EQ(send_result.value(), data_size) << "Total bytes sent should match requested size";

    // Receive via server socket — receive() loops until buffer is full
    std::vector<std::byte> recv_buffer(data_size);
    auto recv_result = co_await server_socket.receive(recv_buffer);
    if (!recv_result) {
      EXPECT_TRUE(recv_result) << recv_result.error();
      co_return -1;
    }
    EXPECT_EQ(recv_result.value(), data_size);

    // Verify data integrity on receive side
    std::string received_data(recv_result.value(), '\0');
    for (size_t i = 0; i < recv_result.value(); ++i) {
      received_data[i] = static_cast<char>(recv_buffer[i]);
    }
    EXPECT_EQ(received_data, send_data);

    // Clean up
    (void)client_socket.close();
    (void)server_socket.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_connection_refused) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Try to connect to a random port with no listener (use port 65432)
    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(65432);
    inet_pton(AF_INET, "127.0.0.1", &target_addr.sin_addr);
    auto connect_result = co_await server::io::iocp_socket::connect_coro(
        reactor, &target_addr, sizeof(target_addr));

    // Should fail because no listener is on that port
    if (connect_result) {
      // If it somehow succeeded, clean up and fail the test
      EXPECT_TRUE(false) << "Expected connection to be refused but it succeeded";
      (void)connect_result->close();
      co_return -1;
    }

    EXPECT_FALSE(connect_result.error().empty()) << "Error message should not be empty";

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_set_no_delay) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Accept the connection on the server side
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Test TCP_NODELAY on client socket
    auto nodelay_enable = client_socket.set_no_delay(true);
    if (!nodelay_enable) {
      EXPECT_TRUE(nodelay_enable) << nodelay_enable.error();
      co_return -1;
    }

    // Test TCP_NODELAY on server socket
    auto nodelay_disable = server_socket.set_no_delay(false);
    if (!nodelay_disable) {
      EXPECT_TRUE(nodelay_disable) << nodelay_disable.error();
      co_return -1;
    }

    // Clean up
    (void)client_socket.close();
    (void)server_socket.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_move_semantics) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Connect a client socket
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto socket1 = std::move(*connect_result);
    EXPECT_FALSE(socket1.is_closed());
    EXPECT_NE(socket1.get_native_handle(), server::io::invalid_socket_id);

    // Move-construct another iocp_socket
    auto socket2 = std::move(socket1);

    // Original should be closed after move
    EXPECT_TRUE(socket1.is_closed());
    EXPECT_EQ(socket1.get_native_handle(), server::io::invalid_socket_id);

    // New one should work
    EXPECT_FALSE(socket2.is_closed());
    EXPECT_NE(socket2.get_native_handle(), server::io::invalid_socket_id);

    // Test move assignment
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto socket3 = std::move(*accept_result);
    EXPECT_FALSE(socket3.is_closed());

    // Move-assign socket2 to socket3
    socket3 = std::move(socket2);

    // socket2 should be closed after move
    EXPECT_TRUE(socket2.is_closed());
    EXPECT_EQ(socket2.get_native_handle(), server::io::invalid_socket_id);

    // socket3 should now hold the moved-to socket
    EXPECT_FALSE(socket3.is_closed());
    EXPECT_NE(socket3.get_native_handle(), server::io::invalid_socket_id);

    // Clean up
    (void)socket3.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, listener_open_close) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Create and open listener
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Verify listener is open
    EXPECT_TRUE(listener.is_open());
    EXPECT_NE(listener.get_native_handle(), server::io::invalid_socket_id);

    // Get the assigned port
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);
    EXPECT_NE(actual_port, 0) << "Port should be auto-assigned and non-zero";

    // Close listener
    auto close_result = listener.close();
    if (!close_result) {
      EXPECT_TRUE(close_result) << close_result.error();
      co_return -1;
    }

    // Verify listener is closed
    EXPECT_FALSE(listener.is_open());
    EXPECT_EQ(listener.get_native_handle(), server::io::invalid_socket_id);

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

TEST(iocp_socket_tests, socket_echo_server) {
  auto& ws_result = server::io::init_winsock();
  ASSERT_TRUE(ws_result) << ws_result.error();

  async_coro::scheduler scheduler;
  auto reactor_result = server::io::iocp_reactor::create();
  ASSERT_TRUE(reactor_result) << "Failed to create IOCP reactor: " << reactor_result.error();
  auto& reactor = *reactor_result;

  auto test = [&]() -> async_coro::task<int> {
    // Open a listener on port 0 (auto-assign port)
    auto listener_result = server::io::iocp_listener::open(reactor, "127.0.0.1", 0);
    if (!listener_result) {
      EXPECT_TRUE(listener_result) << listener_result.error();
      co_return -1;
    }
    server::io::iocp_listener listener = std::move(*listener_result);

    // Get the actual port from getsockname
    sockaddr_in addr{};
    int addr_len = static_cast<int>(sizeof(addr));
    if (getsockname(listener.get_native_handle(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
      EXPECT_TRUE(false) << "getsockname failed";
      co_return -1;
    }
    uint16_t actual_port = ntohs(addr.sin_port);

    // Client connects
    sockaddr_in client_addr{AF_INET, htons(actual_port), addr.sin_addr};
    auto connect_result =
        co_await server::io::iocp_socket::connect_coro(reactor, &client_addr, sizeof(client_addr));
    if (!connect_result) {
      EXPECT_TRUE(connect_result) << connect_result.error();
      co_return -1;
    }

    auto client_socket = std::move(*connect_result);

    // Server accepts
    auto accept_result = co_await listener.accept();
    if (!accept_result) {
      EXPECT_TRUE(accept_result) << accept_result.error();
      co_return -1;
    }

    auto server_socket = std::move(*accept_result);

    // Test multiple round-trips
    constexpr std::array<std::string_view, 5> messages = {
        "Echo test 1",
        "Hello World!",
        "Data packet with special chars: !@#$%",
        "Longer message to test buffer handling in the echo server implementation",
        "Final"};

    for (const auto& msg : messages) {
      // Client sends data
      auto send_result = co_await client_socket.send(std::as_bytes(std::span<const char>(msg)));
      if (!send_result) {
        EXPECT_TRUE(send_result) << send_result.error();
        co_return -1;
      }
      EXPECT_EQ(send_result.value(), msg.size());

      // Server receives and echoes back
      std::array<std::byte, 256> recv_buffer{};
      auto recv_result = co_await server_socket.receive(recv_buffer);
      if (!recv_result) {
        EXPECT_TRUE(recv_result) << recv_result.error();
        co_return -1;
      }
      EXPECT_EQ(recv_result.value(), msg.size());

      // Echo back
      std::vector<std::byte> echo_data(recv_result.value());
      for (size_t i = 0; i < recv_result.value(); ++i) {
        echo_data[i] = recv_buffer[i];
      }
      auto echo_send = co_await server_socket.send(echo_data);
      if (!echo_send) {
        EXPECT_TRUE(echo_send) << echo_send.error();
        co_return -1;
      }
      EXPECT_EQ(echo_send.value(), msg.size());

      // Client receives echoed data and verifies
      std::array<std::byte, 256> client_recv_buffer{};
      auto client_recv = co_await client_socket.receive(client_recv_buffer);
      if (!client_recv) {
        EXPECT_TRUE(client_recv) << client_recv.error();
        co_return -1;
      }
      EXPECT_EQ(client_recv.value(), msg.size());

      // Verify received data matches sent data
      std::string received_data(client_recv.value(), '\0');
      for (size_t i = 0; i < client_recv.value(); ++i) {
        received_data[i] = static_cast<char>(client_recv_buffer[i]);
      }
      EXPECT_EQ(received_data, msg);
    }

    // Clean up
    (void)client_socket.close();
    (void)server_socket.close();

    co_return 0;
  };

  ASSERT_TRUE(test_utils::run_task_iocp(test(), scheduler, reactor));
}

#endif  // WIN_IOCP_ENABLED
