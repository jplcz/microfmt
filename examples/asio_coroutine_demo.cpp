// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Demonstrates `microfmt::coro::async_format_to`: formatting a message
// directly into a coroutine-frame stack buffer and streaming it over a
// Boost.Asio socket with zero heap allocations, using C++20 coroutines.

#include <array>
#include <cstdio>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>

#include <microfmt/sinks/asio_coroutine.hpp>

namespace {
using boost::asio::ip::tcp;

boost::asio::awaitable<void> server(tcp::acceptor &acceptor) {
  tcp::socket socket = co_await acceptor.async_accept(boost::asio::use_awaitable);

  std::array<char, 128> reply{};
  const std::size_t reply_size = co_await boost::asio::async_read(
      socket, boost::asio::buffer(reply), boost::asio::transfer_at_least(1), boost::asio::use_awaitable);

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("server received: %.*s\n", static_cast<int>(reply_size), reply.data());

  MICROFMT_END_UNSAFE_BUFFER_USAGE;
}

boost::asio::awaitable<void> client(unsigned short port) {
  auto executor = co_await boost::asio::this_coro::executor;
  tcp::socket socket(executor);
  co_await socket.async_connect(tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), port),
                                boost::asio::use_awaitable);

  // Formats "reading={} unit={}" into a 128-byte stack buffer, then
  // asynchronously writes the exact formatted bytes with no heap allocation.
  const std::size_t bytes_written =
      co_await microfmt::coro::async_format_to<tcp::socket, 128>(socket, "reading={} unit=C", 24);

  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("client wrote %zu bytes\n", bytes_written);

  MICROFMT_END_UNSAFE_BUFFER_USAGE;
}

} // namespace

int main() {
  boost::asio::io_context io_context;

  tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
  const unsigned short port = acceptor.local_endpoint().port();

  boost::asio::co_spawn(io_context, server(acceptor), boost::asio::detached);
  boost::asio::co_spawn(io_context, client(port), boost::asio::detached);

  io_context.run();
}
