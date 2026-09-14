// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

/**
 * @file cbor.hpp
 * @brief Stream indefinite-length CBOR maps and arrays to a microfmt sink.
 *
 * Use @c map_writer and @c array_writer for scoped CBOR serialization, or
 * @c cbor_map to embed a map-writing lambda in a format string.
 */

namespace microfmt::cbor {

// ============================================================================
// CBOR Major Types & Control Constants
// ============================================================================

namespace detail {
inline constexpr uint8_t MT_UNSIGNED = 0x00; // Major 0
inline constexpr uint8_t MT_NEGATIVE = 0x20; // Major 1
inline constexpr uint8_t MT_BYTES = 0x40;    // Major 2
inline constexpr uint8_t MT_TEXT = 0x60;     // Major 3
inline constexpr uint8_t MT_ARRAY = 0x80;    // Major 4
inline constexpr uint8_t MT_MAP = 0xA0;      // Major 5
inline constexpr uint8_t MT_SIMPLE = 0xE0;   // Major 7

inline constexpr uint8_t BREAK_BYTE =
    0xFF; // Stop code for indefinite containers

inline void encode_header(const sink &out, uint8_t major,
                          uint64_t val) noexcept {
  if (val < 24) {
    out.put(static_cast<char>(major | static_cast<uint8_t>(val)));
  } else if (val <= 0xFF) {
    out.put(static_cast<char>(major | 24));
    out.put(static_cast<char>(val));
  } else if (val <= 0xFFFF) {
    out.put(static_cast<char>(major | 25));
    out.put(static_cast<char>((val >> 8) & 0xFF));
    out.put(static_cast<char>(val & 0xFF));
  } else if (val <= 0xFFFFFFFF) {
    out.put(static_cast<char>(major | 26));
    out.put(static_cast<char>((val >> 24) & 0xFF));
    out.put(static_cast<char>((val >> 16) & 0xFF));
    out.put(static_cast<char>((val >> 8) & 0xFF));
    out.put(static_cast<char>(val & 0xFF));
  } else {
    out.put(static_cast<char>(major | 27));
    for (int shift = 56; shift >= 0; shift -= 8) {
      out.put(static_cast<char>((val >> shift) & 0xFF));
    }
  }
}
} // namespace detail

// ============================================================================
// RAII Scoped Map & Array Writers (Indefinite-Length Streaming)
// ============================================================================

class array_writer;

/** @brief RAII writer for an indefinite-length streaming CBOR map. */
class map_writer {
public:
  explicit map_writer(sink out) noexcept : out_(std::move(out)) {
    // 0xBF: Indefinite-length map
    out_.put(static_cast<char>(detail::MT_MAP | 31));
  }

  ~map_writer() noexcept { end(); }

  map_writer(const map_writer &) = delete;
  map_writer &operator=(const map_writer &) = delete;
  map_writer(map_writer &&other) noexcept
      : out_(std::move(other.out_)), closed_(other.closed_) {
    other.closed_ = true;
  }

  // Key emitters (supports both Text keys and Integer/Tag keys for compact
  // frames)
  map_writer &key(std::string_view k) noexcept {
    detail::encode_header(out_, detail::MT_TEXT, k.size());
    out_.write(k);
    return *this;
  }

  map_writer &key(uint32_t int_key) noexcept {
    detail::encode_header(out_, detail::MT_UNSIGNED, int_key);
    return *this;
  }

  // Key-Value primitives (String Key)
  map_writer &kv(std::string_view k, std::string_view val) noexcept {
    key(k);
    detail::encode_header(out_, detail::MT_TEXT, val.size());
    out_.write(val);
    return *this;
  }

  map_writer &kv(std::string_view k, const char *val) noexcept {
    key(k);
    if (val == nullptr) {
      out_.put(static_cast<char>(detail::MT_SIMPLE | 22));
    } else {
      detail::encode_header(out_, detail::MT_TEXT,
                            std::string_view(val).size());
      out_.write(val);
    }
    return *this;
  }

  template <std::size_t N>
  map_writer &kv(std::string_view k, const char (&val)[N]) noexcept {
    return kv(k, std::string_view(val, N - 1));
  }

  map_writer &kv(std::string_view k, span<const uint8_t> bytes) noexcept {
    key(k);
    detail::encode_header(out_, detail::MT_BYTES, bytes.size());
    for (uint8_t b : bytes)
      out_.put(static_cast<char>(b));
    return *this;
  }

  map_writer &kv(std::string_view k, bool val) noexcept {
    key(k);
    out_.put(static_cast<char>(detail::MT_SIMPLE |
                               (val ? 21 : 20))); // 0xF5 (true), 0xF4 (false)
    return *this;
  }

  map_writer &kv(std::string_view k, std::nullptr_t) noexcept {
    key(k);
    out_.put(static_cast<char>(detail::MT_SIMPLE | 22)); // 0xF6 (null)
    return *this;
  }

  template <typename T,
            typename std::enable_if<std::is_integral<T>::value &&
                                        !std::is_same<T, bool>::value,
                                    int>::type = 0>
  map_writer &kv(std::string_view k, T val) noexcept {
    key(k);
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        detail::encode_header(out_, detail::MT_NEGATIVE,
                              static_cast<uint64_t>(-1 - val));
      } else {
        detail::encode_header(out_, detail::MT_UNSIGNED,
                              static_cast<uint64_t>(val));
      }
    } else {
      detail::encode_header(out_, detail::MT_UNSIGNED,
                            static_cast<uint64_t>(val));
    }
    return *this;
  }

  // Key-Value primitives (Compact Integer Key)
  template <typename T,
            typename std::enable_if<std::is_integral<T>::value &&
                                        !std::is_same<T, bool>::value,
                                    int>::type = 0>
  map_writer &kv(uint32_t k, T val) noexcept {
    key(k);
    if constexpr (std::is_signed_v<T>) {
      if (val < 0) {
        detail::encode_header(out_, detail::MT_NEGATIVE,
                              static_cast<uint64_t>(-1 - val));
      } else {
        detail::encode_header(out_, detail::MT_UNSIGNED,
                              static_cast<uint64_t>(val));
      }
    } else {
      detail::encode_header(out_, detail::MT_UNSIGNED,
                            static_cast<uint64_t>(val));
    }
    return *this;
  }

  [[nodiscard]] map_writer nested_map(std::string_view k) noexcept {
    key(k);
    return map_writer(out_);
  }

  [[nodiscard]] array_writer nested_array(std::string_view k) noexcept;

  void end() noexcept {
    if (!closed_) {
      out_.put(static_cast<char>(detail::BREAK_BYTE)); // 0xFF
      closed_ = true;
    }
  }

private:
  sink out_;
  bool closed_{false};
};

/** @brief RAII writer for an indefinite-length streaming CBOR array. */
class array_writer {
public:
  explicit array_writer(sink out) noexcept : out_(std::move(out)) {
    // 0x9F: Indefinite-length array
    out_.put(static_cast<char>(detail::MT_ARRAY | 31));
  }

  ~array_writer() noexcept { end(); }

  array_writer(const array_writer &) = delete;
  array_writer &operator=(const array_writer &) = delete;
  array_writer(array_writer &&other) noexcept
      : out_(std::move(other.out_)), closed_(other.closed_) {
    other.closed_ = true;
  }

  array_writer &val(std::string_view v) noexcept {
    detail::encode_header(out_, detail::MT_TEXT, v.size());
    out_.write(v);
    return *this;
  }

  array_writer &val(const char *v) noexcept {
    if (v == nullptr) {
      out_.put(static_cast<char>(detail::MT_SIMPLE | 22));
    } else {
      const std::string_view text(v);
      detail::encode_header(out_, detail::MT_TEXT, text.size());
      out_.write(text);
    }
    return *this;
  }

  template <std::size_t N> array_writer &val(const char (&v)[N]) noexcept {
    return val(std::string_view(v, N - 1));
  }

  array_writer &val(span<const uint8_t> bytes) noexcept {
    detail::encode_header(out_, detail::MT_BYTES, bytes.size());
    for (uint8_t b : bytes)
      out_.put(static_cast<char>(b));
    return *this;
  }

  array_writer &val(bool v) noexcept {
    out_.put(static_cast<char>(detail::MT_SIMPLE | (v ? 21 : 20)));
    return *this;
  }

  array_writer &val(std::nullptr_t) noexcept {
    out_.put(static_cast<char>(detail::MT_SIMPLE | 22));
    return *this;
  }

  template <typename T,
            typename std::enable_if<std::is_integral<T>::value &&
                                        !std::is_same<T, bool>::value,
                                    int>::type = 0>
  array_writer &val(T v) noexcept {
    if constexpr (std::is_signed_v<T>) {
      if (v < 0) {
        detail::encode_header(out_, detail::MT_NEGATIVE,
                              static_cast<uint64_t>(-1 - v));
      } else {
        detail::encode_header(out_, detail::MT_UNSIGNED,
                              static_cast<uint64_t>(v));
      }
    } else {
      detail::encode_header(out_, detail::MT_UNSIGNED,
                            static_cast<uint64_t>(v));
    }
    return *this;
  }

  [[nodiscard]] map_writer map() noexcept { return map_writer(out_); }

  void end() noexcept {
    if (!closed_) {
      out_.put(static_cast<char>(detail::BREAK_BYTE)); // 0xFF
      closed_ = true;
    }
  }

private:
  sink out_;
  bool closed_{false};
};

inline array_writer map_writer::nested_array(std::string_view k) noexcept {
  key(k);
  return array_writer(out_);
}

// ============================================================================
// Lambda View Adapter for microfmt Integration
// ============================================================================

template <typename Fn> struct cbor_map_view {
  Fn fn;
};

/** @brief Wraps a map-writing callable for use as a format argument. */
template <typename Fn> [[nodiscard]] constexpr auto cbor_map(Fn &&fn) noexcept {
  return cbor_map_view<std::decay_t<Fn>>{std::forward<Fn>(fn)};
}

} // namespace microfmt::cbor

namespace microfmt {

template <typename Fn> struct formatter<cbor::cbor_map_view<Fn>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const cbor::cbor_map_view<Fn> &v,
              const sink &out) const noexcept {
    cbor::map_writer w(out);
    v.fn(w);
  }
};

} // namespace microfmt