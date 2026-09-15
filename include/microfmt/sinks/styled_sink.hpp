// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file styled_sink.hpp
 *  @brief Zero-buffer output sink adapters for text transformation and layout.
 *
 *  Wrap a destination @ref microfmt::sink with @ref microfmt::transform_sink,
 *  @ref microfmt::prefix_sink, or @ref microfmt::limit_sink to transform text,
 *  prefix each output line, or enforce a hard output-size limit while data is
 *  streamed.
 */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace microfmt {

/** Per-character transformation applied by @ref transform_sink. */
enum class char_transform : uint8_t {
  none = 0,
  to_upper,
  to_lower,
  sanitize_ascii // Replaces non-printable control chars with '.'
};

/** A sink adapter that transforms characters before writing to its target.
 *
 *  Case transforms affect ASCII letters only. @ref char_transform::sanitize_ascii
 *  replaces ASCII control characters except newline, carriage return, and tab
 *  with a period.
 */
class transform_sink {
public:
  /** Create a transform adapter that forwards to @p target. */
  explicit constexpr transform_sink(
      sink target, char_transform t = char_transform::none) noexcept
      : target_(target), transform_(t) {}

  transform_sink(const transform_sink &) = delete;
  transform_sink &operator=(const transform_sink &) = delete;
  transform_sink(transform_sink &&) noexcept = default;
  transform_sink &operator=(transform_sink &&) noexcept = default;

  /** Return a type-erased sink suitable for @ref format_to. */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<transform_sink *>(ctx)->write(sv);
                }};
  }

  /** Change the transformation applied to subsequent output. */
  void set_transform(char_transform t) noexcept { transform_ = t; }

  void put(char c) noexcept { target_.put(apply_char(c)); }

  void write(std::string_view sv) noexcept {
    for (char c : sv) {
      target_.put(apply_char(c));
    }
  }

private:
  [[nodiscard]] constexpr char apply_char(char c) const noexcept {
    switch (transform_) {
    case char_transform::to_upper:
      if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - ('a' - 'A'));
      return c;
    case char_transform::to_lower:
      if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
      return c;
    case char_transform::sanitize_ascii:
      if ((static_cast<uint8_t>(c) < 0x20 || c == '\x7f') && c != '\n' &&
          c != '\r' &&
          c != '\t') {
        return '.';
      }
      return c;
    case char_transform::none:
    default:
      return c;
    }
  }

  sink target_;
  char_transform transform_{char_transform::none};
};

/** A sink adapter that writes a prefix at the start of each output line.
 *
 *  The configured prefix is emitted lazily, before the first character of a
 *  line. This preserves line state when formatting is split across multiple
 *  calls.
 */
class prefix_sink {
public:
  /** Create a line-prefix adapter that forwards to @p target. */
  explicit constexpr prefix_sink(sink target,
                                 std::string_view prefix = "  ") noexcept
      : target_(target), prefix_(prefix) {}

  prefix_sink(const prefix_sink &) = delete;
  prefix_sink &operator=(const prefix_sink &) = delete;
  prefix_sink(prefix_sink &&) noexcept = default;
  prefix_sink &operator=(prefix_sink &&) noexcept = default;

  /** Return a type-erased sink suitable for @ref format_to. */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<prefix_sink *>(ctx)->write(sv);
                }};
  }

  /** Change the prefix used for subsequent lines. */
  void set_prefix(std::string_view p) noexcept { prefix_ = p; }

  void put(char c) noexcept {
    if (at_line_start_) {
      target_.write(prefix_);
      at_line_start_ = false;
    }

    target_.put(c);

    if (c == '\n') {
      at_line_start_ = true;
    }
  }

  void write(std::string_view sv) noexcept {
    for (char c : sv) {
      put(c);
    }
  }

private:
  sink target_;
  std::string_view prefix_{"  "};
  bool at_line_start_{true};
};

/** A sink adapter that discards output after a fixed number of bytes. */
class limit_sink {
public:
  /** Create an adapter that forwards at most @p max_bytes to @p target. */
  explicit constexpr limit_sink(sink target, size_t max_bytes) noexcept
      : target_(target), remaining_(max_bytes) {}

  limit_sink(const limit_sink &) = delete;
  limit_sink &operator=(const limit_sink &) = delete;
  limit_sink(limit_sink &&) noexcept = default;
  limit_sink &operator=(limit_sink &&) noexcept = default;

  /** Return a type-erased sink suitable for @ref format_to. */
  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<limit_sink *>(ctx)->write(sv);
                }};
  }

  void put(char c) noexcept {
    if (remaining_ > 0) {
      target_.put(c);
      --remaining_;
    }
  }

  void write(std::string_view sv) noexcept {
    if (remaining_ == 0 || sv.empty())
      return;

    const size_t to_write = (sv.size() <= remaining_) ? sv.size() : remaining_;
    target_.write(sv.substr(0, to_write));
    remaining_ -= to_write;
  }

  /** Return the number of bytes that can still be forwarded. */
  [[nodiscard]] constexpr size_t remaining() const noexcept {
    return remaining_;
  }
  /** Return true when the configured byte limit has been reached. */
  [[nodiscard]] constexpr bool capped() const noexcept {
    return remaining_ == 0;
  }

private:
  sink target_;
  size_t remaining_{0};
};

} // namespace microfmt