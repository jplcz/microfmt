// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file string_pool_ref.hpp
 * @brief The type-erased face of a `string_pool`: `string_id`, `string_pool_ref`, and string-literal decoders.
 *
 * Why: `token_stream` and the parser helpers must work with any `string_pool<N>` without becoming templates.
 * `string_pool_ref` is the same pattern as `window_ref`: three `function_ref`s bound to one pool. This header
 * also holds the ready-made decoders (`decode_json_string`, `decode_c_escapes`, `decode_sql_string`) that
 * `expect_string` runs on the body of a string literal. See `docs/token-stream.md` ("Streaming input").
 */

#include "lexer.hpp"
#include <cstddef>
#include <cstdint>
#include <reloco/function_ref.hpp>
#include <reloco/string_view.hpp>

namespace microfmt {

/** Index of a string inside a `string_pool`. Four bytes, trivially copyable; `invalid()` means "no string". */
struct RELOCO_COPYABLE string_id {
  static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

  uint32_t index{kInvalid};

  [[nodiscard]] static constexpr string_id invalid() noexcept { return string_id{}; }
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalid; }

  friend constexpr bool operator==(string_id a, string_id b) noexcept { return a.index == b.index; }
  friend constexpr bool operator!=(string_id a, string_id b) noexcept { return a.index != b.index; }
};

/** Outcome of an operation on a pooled string: an id, or a static error message when `error` is not empty. */
struct RELOCO_POINTER pool_result {
  string_id id{};
  microfmt::string_view error{};

  [[nodiscard]] constexpr bool ok() const noexcept { return error.empty(); }
};

/**
 * @brief Where a decoder writes the text it produces; a non-template stand-in for `string_pool::builder`.
 *
 * A decoder calls `append`/`push_back` for each piece of decoded text and `fail("message")` (a string literal) on
 * bad input. After `fail` further appends are ignored and the whole literal is rejected.
 */
class RELOCO_POINTER string_sink {
public:
  explicit string_sink(reloco::function_ref<void(microfmt::string_view)> append) noexcept : append_(append) {}

  void append(microfmt::string_view piece) noexcept {
    if (failure_.empty()) {
      append_(piece);
    }
  }
  void push_back(char c) noexcept { append(microfmt::string_view(&c, 1)); }

  /** Rejects the literal. `message` must have static storage. The first failure wins. */
  void fail(microfmt::string_view message) noexcept {
    if (failure_.empty()) {
      failure_ = message;
    }
  }

  [[nodiscard]] bool failed() const noexcept { return !failure_.empty(); }
  [[nodiscard]] microfmt::string_view failure() const noexcept { return failure_; }

private:
  reloco::function_ref<void(microfmt::string_view)> append_;
  microfmt::string_view failure_{};
};

/** `void(string_sink &out, string_view raw_body)`: turns the text between the quotes into the value. */
using string_decoder = reloco::function_ref<void(string_sink &, microfmt::string_view)>;

/** `string_pool` as the stream and the helpers see it. Build one with `string_pool::ref()`. */
class RELOCO_POINTER string_pool_ref {
public:
  using intern_fn = reloco::function_ref<pool_result(microfmt::string_view)>;
  using decode_fn = reloco::function_ref<pool_result(microfmt::string_view, string_decoder)>;
  using view_fn = reloco::function_ref<microfmt::string_view(string_id)>;

  string_pool_ref(intern_fn intern, decode_fn decode, view_fn view) noexcept
      : intern_(intern), decode_(decode), view_(view) {}

  /** The id of `text`, adding it when new. */
  [[nodiscard]] pool_result intern(microfmt::string_view text) const noexcept { return intern_(text); }

  /** Runs `decoder` over `raw` and interns what it wrote. A failed decode leaves nothing in the pool. */
  [[nodiscard]] pool_result decode(microfmt::string_view raw, string_decoder decoder) const noexcept {
    return decode_(raw, decoder);
  }

  /** The text of `id`; valid until the pool changes. */
  [[nodiscard]] microfmt::string_view view(string_id id) const noexcept { return view_(id); }

private:
  intern_fn intern_;
  decode_fn decode_;
  view_fn view_;
};

namespace detail {

[[nodiscard]] constexpr int hex_digit_value(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Reads `count` hex digits at raw[index]; returns false when they are missing or not hex.
[[nodiscard]] inline bool read_hex(microfmt::string_view raw, size_t index, size_t count, uint32_t &value) noexcept {
  if (index > raw.size() || count > raw.size() - index) {
    return false;
  }
  uint32_t result = 0;
  for (size_t i = 0; i < count; ++i) {
    const int digit = hex_digit_value(raw[index + i]);
    if (digit < 0) {
      return false;
    }
    result = (result << 4) | static_cast<uint32_t>(digit & 0xF);
  }
  value = result;
  return true;
}

[[nodiscard]] constexpr char byte_char(uint32_t value) noexcept {
  return static_cast<char>(static_cast<uint8_t>(value & 0xFFu));
}

inline void put_utf8(string_sink &out, uint32_t cp) noexcept {
  if (cp < 0x80u) {
    out.push_back(byte_char(cp));
  } else if (cp < 0x800u) {
    out.push_back(byte_char(0xC0u | (cp >> 6)));
    out.push_back(byte_char(0x80u | (cp & 0x3Fu)));
  } else if (cp < 0x10000u) {
    out.push_back(byte_char(0xE0u | (cp >> 12)));
    out.push_back(byte_char(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(byte_char(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(byte_char(0xF0u | (cp >> 18)));
    out.push_back(byte_char(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(byte_char(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(byte_char(0x80u | (cp & 0x3Fu)));
  }
}

// Copies the run of non-backslash bytes starting at `i` and returns the index of the next backslash (or the end).
[[nodiscard]] inline size_t copy_plain_run(string_sink &out, microfmt::string_view raw, size_t i) noexcept {
  size_t j = i;
  while (j < raw.size() && raw[j] != '\\') {
    ++j;
  }
  out.append(raw.substr(i, j - i));
  return j;
}

} // namespace detail

/** C-style escapes: `\n \t \r \a \b \f \v \0 \" \' \\` and `\xHH` (one byte). Anything else is a bad escape. */
inline void decode_c_escapes(string_sink &out, microfmt::string_view raw) noexcept {
  size_t i = 0;
  while (i < raw.size() && !out.failed()) {
    i = detail::copy_plain_run(out, raw, i);
    if (i >= raw.size()) {
      return;
    }
    if (i + 1 >= raw.size()) {
      out.fail("bad escape sequence");
      return;
    }
    const char e = raw[i + 1];
    i += 2;
    switch (e) {
    case 'n':
      out.push_back('\n');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 'a':
      out.push_back('\a');
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'v':
      out.push_back('\v');
      break;
    case '0':
      out.push_back('\0');
      break;
    case '"':
      out.push_back('"');
      break;
    case '\'':
      out.push_back('\'');
      break;
    case '\\':
      out.push_back('\\');
      break;
    case 'x': {
      uint32_t value = 0;
      if (!detail::read_hex(raw, i, 2, value)) {
        out.fail("bad \\x escape");
        return;
      }
      out.push_back(detail::byte_char(value));
      i += 2;
      break;
    }
    default:
      out.fail("bad escape sequence");
      return;
    }
  }
}

/** JSON escapes: `\" \\ \/ \b \f \n \r \t` and `\uXXXX` (UTF-8 output; a surrogate pair gives one code point). */
inline void decode_json_string(string_sink &out, microfmt::string_view raw) noexcept {
  size_t i = 0;
  while (i < raw.size() && !out.failed()) {
    i = detail::copy_plain_run(out, raw, i);
    if (i >= raw.size()) {
      return;
    }
    if (i + 1 >= raw.size()) {
      out.fail("bad escape sequence");
      return;
    }
    const char e = raw[i + 1];
    i += 2;
    switch (e) {
    case '"':
      out.push_back('"');
      break;
    case '\\':
      out.push_back('\\');
      break;
    case '/':
      out.push_back('/');
      break;
    case 'b':
      out.push_back('\b');
      break;
    case 'f':
      out.push_back('\f');
      break;
    case 'n':
      out.push_back('\n');
      break;
    case 'r':
      out.push_back('\r');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'u': {
      uint32_t cp = 0;
      if (!detail::read_hex(raw, i, 4, cp)) {
        out.fail("bad \\u escape");
        return;
      }
      i += 4;
      if (cp >= 0xD800u && cp <= 0xDBFFu) {
        uint32_t low = 0;
        if (i + 1 < raw.size() && raw[i] == '\\' && raw[i + 1] == 'u' && detail::read_hex(raw, i + 2, 4, low) &&
            low >= 0xDC00u && low <= 0xDFFFu) {
          cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
          i += 6;
        } else {
          out.fail("unpaired surrogate in \\u escape");
          return;
        }
      } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
        out.fail("unpaired surrogate in \\u escape");
        return;
      }
      detail::put_utf8(out, cp);
      break;
    }
    default:
      out.fail("bad escape sequence");
      return;
    }
  }
}

/** SQL strings: a doubled single quote (`''`) is one quote; there are no other escapes. */
inline void decode_sql_string(string_sink &out, microfmt::string_view raw) noexcept {
  size_t i = 0;
  while (i < raw.size()) {
    size_t j = i;
    while (j < raw.size() && raw[j] != '\'') {
      ++j;
    }
    out.append(raw.substr(i, j - i));
    if (j >= raw.size()) {
      return;
    }
    if (j + 1 >= raw.size() || raw[j + 1] != '\'') {
      out.fail("unescaped quote in string");
      return;
    }
    out.push_back('\'');
    i = j + 2;
  }
}

} // namespace microfmt
