#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Escaped View Configuration & Adapter
// ============================================================================

struct escaped_view {
  span<const char> data{};
  bool quote{true};         // Wrap string in double quotes "..."
  bool escape_quotes{true}; // Escape inner " as \"
};

// ============================================================================
// Factory Functions
// ============================================================================

// From std::string_view
[[nodiscard]] constexpr escaped_view
escaped(std::string_view sv, bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(sv.data(), sv.size()), quote,
                      escape_quotes};
}

// From raw const char* buffer / length
[[nodiscard]] constexpr escaped_view
escaped(const char *str, size_t len, bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(str, len), quote, escape_quotes};
}

// From span of raw bytes (const uint8_t)
[[nodiscard]] inline escaped_view escaped(span<const uint8_t> bytes,
                                          bool quote = true,
                                          bool escape_quotes = true) noexcept {
  return escaped_view{
      span<const char>(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size()),
      quote, escape_quotes};
}

// From C-style string literals / char arrays
template <size_t N>
[[nodiscard]] constexpr escaped_view
escaped(const char (&arr)[N], bool quote = true,
        bool escape_quotes = true) noexcept {
  // If null-terminated string literal, exclude trailing \0 from the slice
  // length
  const size_t len = (N > 0 && arr[N - 1] == '\0') ? N - 1 : N;
  return escaped_view{span<const char>(arr, len), quote, escape_quotes};
}

// From C-style byte arrays (uint8_t[N])
template <size_t N>
[[nodiscard]] constexpr escaped_view
escaped(const uint8_t (&arr)[N], bool quote = true,
        bool escape_quotes = true) noexcept {
  return escaped_view{span<const char>(reinterpret_cast<const char *>(arr), N),
                      quote, escape_quotes};
}

// ============================================================================
// Formatter Specialization for escaped_view
// ============================================================================

template <> struct formatter<escaped_view> {
  constexpr void parse(format_parse_context &ctx) noexcept { (void)ctx; }

  void format(const escaped_view &ev, const sink &out) const noexcept {
    if (ev.quote) {
      out.put('"');
    }

    for (const char ch : ev.data) {
      const uint8_t b = static_cast<uint8_t>(ch);
      switch (b) {
      case '\0':
        out.write("\\0");
        break;
      case '\a':
        out.write("\\a");
        break;
      case '\b':
        out.write("\\b");
        break;
      case '\t':
        out.write("\\t");
        break;
      case '\n':
        out.write("\\n");
        break;
      case '\v':
        out.write("\\v");
        break;
      case '\f':
        out.write("\\f");
        break;
      case '\r':
        out.write("\\r");
        break;
      case '\\':
        out.write("\\\\");
        break;
      case '"':
        if (ev.escape_quotes) {
          out.write("\\\"");
        } else {
          out.put('"');
        }
        break;
      default:
        if (b >= 32 && b <= 126) {
          // Printable ASCII
          out.put(static_cast<char>(b));
        } else {
          // Non-printable byte -> \xHH
          out.write("\\x");
          detail::format_unsigned(out, b, 16, false, 2);
        }
        break;
      }
    }

    if (ev.quote) {
      out.put('"');
    }
  }
};

} // namespace microfmt