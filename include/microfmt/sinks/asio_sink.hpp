// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file asio_sink.hpp @brief Boost.Asio buffer and stream sink adapters for zero-allocation formatting. */

#include "../lifetime.hpp"
#include "../microfmt.hpp"
#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>
#include <cstddef>
#include <cstring>
#include <ostream>

namespace microfmt {

/**
 * @brief A sink adapter that writes formatted output directly into a
 * `boost::asio::mutable_buffer`.
 *
 * Wraps an externally provided buffer view (e.g. a coroutine's stack buffer)
 * and exposes a non-owning, zero-allocation @ref sink interface, mirroring
 * @ref span_sink. Tracks remaining capacity and truncates writes that exceed
 * it instead of overflowing.
 */
class RELOCO_POINTER asio_mutable_buffer_sink {
public:
  /**
   * @brief Constructs a sink over a Boost.Asio mutable buffer.
   *
   * @param buf Target mutable buffer. The memory it refers to must outlive
   * this sink, and (via @ref written_buffer) any pending async write that
   * consumes its result.
   */
  explicit asio_mutable_buffer_sink(boost::asio::mutable_buffer buf RELOCO_LIFETIMEBOUND
                                         RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : data_(static_cast<char *>(buf.data())), capacity_(buf.size()) {}

  asio_mutable_buffer_sink(const asio_mutable_buffer_sink &) = delete;
  asio_mutable_buffer_sink &operator=(const asio_mutable_buffer_sink &) = delete;
  asio_mutable_buffer_sink(asio_mutable_buffer_sink &&) noexcept = default;
  asio_mutable_buffer_sink &operator=(asio_mutable_buffer_sink &&) noexcept = default;

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   */
  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND { return sink{this, &write_impl}; }

  /**
   * @brief Returns the total number of bytes written to the buffer so far.
   */
  [[nodiscard]] size_t size() const noexcept { return size_; }

  /**
   * @brief Returns the written prefix of the wrapped buffer as a
   * `boost::asio::const_buffer`, suitable for `boost::asio::write` /
   * `boost::asio::async_write`.
   *
   * The returned buffer aliases this sink's backing storage and must not
   * outlive it.
   */
  [[nodiscard]] boost::asio::const_buffer written_buffer() const noexcept RELOCO_LIFETIMEBOUND {
    return boost::asio::const_buffer(data_, size_);
  }

private:
  static void write_impl(void *ctx, microfmt::string_view str) noexcept {
    auto *self = static_cast<asio_mutable_buffer_sink *>(ctx);

    if (self->size_ >= self->capacity_)
      return;

    size_t to_copy = str.size();
    if (self->size_ + to_copy > self->capacity_) {
      to_copy = self->capacity_ - self->size_;
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    std::memcpy(self->data_ + self->size_, str.data(), to_copy);
    RELOCO_END_UNSAFE_BUFFER_USAGE;

    self->size_ += to_copy;
  }

  char *data_;
  size_t capacity_;
  size_t size_{0};
};

/**
 * @brief A sink adapter that writes formatted output into a
 * `boost::asio::streambuf` dynamic buffer.
 */
class RELOCO_POINTER asio_streambuf_sink {
public:
  /**
   * @brief Constructs a sink over an externally owned Boost.Asio streambuf.
   *
   * @param buf Target streambuf; it must outlive this sink.
   * @param max_chars Maximum number of characters accepted before further
   * writes are silently dropped.
   */
  explicit asio_streambuf_sink(boost::asio::streambuf &buf RELOCO_LIFETIMEBOUND
                                    RELOCO_LIFETIME_CAPTURE_BY_THIS,
                                size_t max_chars = 1024) noexcept
      : streambuf_(&buf), max_chars_(max_chars) {}

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   */
  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND { return sink{this, &write_impl}; }

private:
  static void write_impl(void *ctx, microfmt::string_view str) noexcept {
    auto *self = static_cast<asio_streambuf_sink *>(ctx);

    if (self->written_ >= self->max_chars_)
      return;

    size_t len = str.size();
    if (self->written_ + len > self->max_chars_) {
      len = self->max_chars_ - self->written_;
    }

    // Append via std::ostream so Boost.Asio grows the streambuf as needed.
    std::ostream os(self->streambuf_);
    os.write(str.data(), static_cast<std::streamsize>(len));
    self->written_ += len;
  }

  boost::asio::streambuf *streambuf_;
  size_t max_chars_;
  size_t written_{0};
};

} // namespace microfmt
