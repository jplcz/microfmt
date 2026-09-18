// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/**
 * @file asio_coroutine.hpp
 * @brief C++20 coroutine awaitables for zero-allocation async formatting and
 * writing over Boost.Asio streams.
 */

#include "../lifetime.hpp"
#include "../microfmt.hpp"
#include "asio_sink.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <cstddef>
#include <utility>

namespace microfmt::coro {

/**
 * @brief Formats a message directly into a stack buffer and asynchronously
 * writes it to a Boost.Asio stream.
 *
 * @tparam Stream Async stream type (e.g., `boost::asio::ip::tcp::socket`,
 * `boost::asio::serial_port`).
 * @tparam StackSize Size of the local stack buffer used for formatting.
 * @tparam Args Formatting argument types.
 * @param stream Target asynchronous stream. Must remain valid until the
 * returned awaitable completes; the coroutine holds a reference to it
 * across suspension points.
 * @param fmt Format string view.
 * @param args Arguments to format.
 * @return `boost::asio::awaitable<std::size_t>` resolving to the number of
 * bytes written upon completion.
 */
template <typename Stream, std::size_t StackSize = 256, typename... Args>
[[nodiscard]] boost::asio::awaitable<std::size_t> async_format_to(Stream &stream, string_view fmt, Args &&...args) {
  // Allocate a temporary buffer entirely on the coroutine frame / stack.
  char stack_buffer[StackSize];

  // Wrap it in our zero-allocation Boost.Asio mutable buffer sink; the sink
  // only borrows stack_buffer, so it must not outlive this coroutine frame.
  asio_mutable_buffer_sink sink(boost::asio::buffer(stack_buffer));

  // Perform zero-heap formatting directly into the stack buffer.
  microfmt::format_to(sink.as_sink(), fmt, std::forward<Args>(args)...);

  // Asynchronously stream the exact written bytes using C++20 co_await.
  co_return co_await boost::asio::async_write(stream, sink.written_buffer(), boost::asio::use_awaitable);
}

} // namespace microfmt::coro
