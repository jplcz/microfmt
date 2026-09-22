// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

/**
 * @file json.hpp
 * @brief Stream JSON objects and arrays directly to a microfmt sink.
 *
 * Use @c object_writer and @c array_writer for scoped JSON serialization, or
 * @c json_obj to embed an object-writing lambda in a format string.
 */

namespace microfmt::json {

namespace detail {

template <typename T> void write_integer(const sink &out, T value) noexcept {
  using unsigned_type = typename std::make_unsigned<T>::type;
  if constexpr (std::is_signed<T>::value) {
    if (value < 0) {
      out.put('-');
      const auto magnitude = static_cast<unsigned_type>(0) - static_cast<unsigned_type>(value);
      microfmt::detail::format_unsigned<microfmt::detail::radix::decimal>(out, static_cast<uint64_t>(magnitude), false, 0);
      return;
    }
  }
  microfmt::detail::format_unsigned<microfmt::detail::radix::decimal>(out, static_cast<uint64_t>(static_cast<unsigned_type>(value)), false, 0);
}

} // namespace detail

// ============================================================================
// Escaped String Serializer
// ============================================================================

/** @brief Writes a quoted, JSON-escaped string to @p out. */
inline void write_escaped_string(const sink &out, microfmt::string_view str) noexcept {
  out.put('"');
  for (char c : str) {
    switch (c) {
    case '"':
      out.write("\\\"");
      break;
    case '\\':
      out.write("\\\\");
      break;
    case '\b':
      out.write("\\b");
      break;
    case '\f':
      out.write("\\f");
      break;
    case '\n':
      out.write("\\n");
      break;
    case '\r':
      out.write("\\r");
      break;
    case '\t':
      out.write("\\t");
      break;
    default:
      if (static_cast<uint8_t>(c) < 0x20) {
        // Control character escape: \u00XX
        out.write("\\u00");
        const auto b = static_cast<uint8_t>(c);
        out.put(microfmt::detail::hex_digits_upper[(b >> 4) & 0x0F]);
        out.put(microfmt::detail::hex_digits_upper[b & 0x0F]);
      } else {
        out.put(c);
      }
      break;
    }
  }
  out.put('"');
}

// ============================================================================
// RAII Scoped Array & Object Writers
// ============================================================================

class array_writer;

/** @brief RAII writer for a streaming JSON object.
 *
 * Consumed-state tracked: writes are only valid while `unconsumed`; @ref end
 * transitions the object to `consumed`, after which Clang's `-Wconsumed`
 * flags any further `key`/`kv`/`nested_*` call as a compile-time diagnostic.
 */
class RELOCO_CONSUMABLE(unconsumed) object_writer {
public:
  explicit object_writer(sink out) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : out_(std::move(out)) {
    out_.put('{');
  }

  ~object_writer() noexcept {
    if (!closed_) {
      out_.put('}');
    }
  }

  // Non-copyable, movable
  object_writer(const object_writer &) = delete;
  object_writer &operator=(const object_writer &) = delete;
  object_writer(object_writer &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : out_(std::move(other.out_)), first_(other.first_), closed_(other.closed_) {
    other.closed_ = true;
  }

  // Helper for Clang if object state is unknown
  object_writer &as_known() noexcept RELOCO_CALLABLE_WHEN("unconsumed", "unknown")
      RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(!closed_, "Attempt to reuse consumed state");
    return *this;
  }

  // Key-Value primitives
  object_writer &key(microfmt::string_view k) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    write_escaped_string(out_, k);
    out_.put(':');
    return *this;
  }

  object_writer &kv(microfmt::string_view k, microfmt::string_view val) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    key(k);
    write_escaped_string(out_, val);
    return *this;
  }

  object_writer &kv(microfmt::string_view k, const char *val) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    key(k);
    if (val == nullptr) {
      out_.write("null");
    } else {
      write_escaped_string(out_, val);
    }
    return *this;
  }

  template <std::size_t N>
  object_writer &kv(microfmt::string_view k, const char (&val)[N]) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    return kv(k, microfmt::string_view(val, N - 1));
  }

  object_writer &kv(microfmt::string_view k, bool val) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    key(k);
    out_.write(val ? "true" : "false");
    return *this;
  }

  object_writer &kv(microfmt::string_view k, std::nullptr_t) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    key(k);
    out_.write("null");
    return *this;
  }

  template <typename T,
            typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
  object_writer &kv(microfmt::string_view k, T val) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    key(k);
    detail::write_integer(out_, val);
    return *this;
  }

  // Nested Object
  [[nodiscard]] object_writer nested_object(microfmt::string_view k) noexcept RELOCO_CALLABLE_WHEN("unconsumed")
      RELOCO_RETURN_TYPESTATE(unconsumed) {
    key(k);
    return object_writer(out_);
  }

  // Nested Array
  [[nodiscard]] array_writer nested_array(microfmt::string_view k) noexcept RELOCO_CALLABLE_WHEN("unconsumed")
      RELOCO_RETURN_TYPESTATE(unconsumed);

  void end() noexcept RELOCO_CALLABLE_WHEN("unconsumed", "consumed") RELOCO_SET_TYPESTATE(consumed) {
    if (!closed_) {
      out_.put('}');
      closed_ = true;
    }
  }

private:
  void prefix() noexcept {
    if (!first_) {
      out_.put(',');
    }
    first_ = false;
  }

  sink out_;
  bool first_{true};
  bool closed_{false};
};

/** @brief RAII writer for a streaming JSON array.
 *
 * Consumed-state tracked: see @ref object_writer for the state contract.
 */
class RELOCO_CONSUMABLE(unconsumed) array_writer {
public:
  explicit array_writer(sink out) noexcept RELOCO_RETURN_TYPESTATE(unconsumed) : out_(std::move(out)) {
    out_.put('[');
  }

  ~array_writer() noexcept {
    if (!closed_) {
      out_.put(']');
      closed_ = true;
    }
  }

  array_writer(const array_writer &) = delete;
  array_writer &operator=(const array_writer &) = delete;
  array_writer(array_writer &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : out_(std::move(other.out_)), first_(other.first_), closed_(other.closed_) {
    other.closed_ = true;
  }

  // Helper for Clang if object state is unknown
  array_writer &as_known() noexcept RELOCO_CALLABLE_WHEN("unconsumed", "unknown")
      RELOCO_RETURN_TYPESTATE(unconsumed) {
    RELOCO_ASSERT(!closed_, "Attempt to reuse consumed state");
    return *this;
  }

  array_writer &val(microfmt::string_view v) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    write_escaped_string(out_, v);
    return *this;
  }

  array_writer &val(const char *v) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    if (v == nullptr) {
      out_.write("null");
    } else {
      write_escaped_string(out_, v);
    }
    return *this;
  }

  template <std::size_t N> array_writer &val(const char (&v)[N]) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    return val(microfmt::string_view(v, N - 1));
  }

  array_writer &val(bool v) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    out_.write(v ? "true" : "false");
    return *this;
  }

  array_writer &val(std::nullptr_t) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    out_.write("null");
    return *this;
  }

  template <typename T,
            typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, int>::type = 0>
  array_writer &val(T v) noexcept RELOCO_CALLABLE_WHEN("unconsumed") {
    prefix();
    detail::write_integer(out_, v);
    return *this;
  }

  [[nodiscard]] object_writer obj() noexcept RELOCO_CALLABLE_WHEN("unconsumed")
      RELOCO_RETURN_TYPESTATE(unconsumed) {
    prefix();
    return object_writer(out_);
  }

  void end() noexcept RELOCO_CALLABLE_WHEN("unconsumed", "consumed") RELOCO_SET_TYPESTATE(consumed) {
    if (!closed_) {
      out_.put(']');
      closed_ = true;
    }
  }

private:
  void prefix() noexcept {
    if (!first_) {
      out_.put(',');
    }
    first_ = false;
  }

  sink out_;
  bool first_{true};
  bool closed_{false};
};

inline array_writer object_writer::nested_array(microfmt::string_view k) noexcept {
  key(k);
  return array_writer(out_);
}

// ============================================================================
// Lambda-Based JSON Stream Adapter for format_to
// ============================================================================

template <typename Fn> struct json_obj_view {
  Fn fn;
};

/** @brief Wraps an object-writing callable for use as a format argument. */
template <typename Fn> [[nodiscard]] constexpr auto json_obj(Fn &&fn) noexcept {
  return json_obj_view<std::decay_t<Fn>>{std::forward<Fn>(fn)};
}

} // namespace microfmt::json

namespace microfmt {

template <typename Fn> struct formatter<json::json_obj_view<Fn>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const json::json_obj_view<Fn> &v, const sink &out) const noexcept {
    json::object_writer w(out);
    v.fn(w);
    // Closed by object_writer destructor
  }
};

} // namespace microfmt