#pragma once

/** @file fmt.hpp @brief Lightweight {fmt}-compatible formatting bridge. */

#include "../microfmt.hpp"
#include "ranges.hpp"
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace fmt {

// Default stack buffer size for fmt::format(...) if no template size is
// supplied
inline constexpr size_t DEFAULT_FORMAT_BUFFER_SIZE = 128;

// ============================================================================
// Result & Context Types
// ============================================================================

template <typename OutputIt> struct format_to_n_result {
  OutputIt out;
  size_t size;
};

using string_view = std::string_view;

// Parse context emulating fmt::format_parse_context
struct format_parse_context {
  std::string_view spec;

  constexpr auto begin() const noexcept { return spec.begin(); }
  constexpr auto end() const noexcept { return spec.end(); }
  constexpr void advance_to(std::string_view::iterator it) noexcept {
    spec = std::string_view(it, static_cast<size_t>(spec.end() - it));
  }
};

// ============================================================================
// Output Iterator Adapter wrapping microfmt::sink
// ============================================================================

class sink_output_iterator {
public:
  using iterator_category = std::output_iterator_tag;
  using value_type = void;
  using difference_type = std::ptrdiff_t;
  using pointer = void;
  using reference = void;

  constexpr explicit sink_output_iterator(const microfmt::sink &s) noexcept
      : sink_(&s) {}

  sink_output_iterator &operator=(char c) noexcept {
    sink_->put(c);
    return *this;
  }

  constexpr sink_output_iterator &operator*() noexcept { return *this; }
  constexpr sink_output_iterator &operator++() noexcept { return *this; }
  constexpr sink_output_iterator &operator++(int) noexcept { return *this; }

private:
  const microfmt::sink *sink_;
};

// Minimal emulated format_context
class format_context {
public:
  using iterator = sink_output_iterator;

  constexpr explicit format_context(iterator out) noexcept : out_(out) {}

  constexpr iterator out() const noexcept { return out_; }
  constexpr void advance_to(iterator it) noexcept { out_ = it; }

private:
  iterator out_;
};

// Primary template for fmt::formatter
template <typename T, typename Char = char, typename Enable = void>
struct formatter;

// ============================================================================
// fmt::format_to_n
// ============================================================================

template <typename... Args>
format_to_n_result<char *> format_to_n(char *out, size_t n,
                                       std::string_view fmt_str,
                                       const Args &...args) noexcept {
  struct BoundedBufferState {
    char *data;
    size_t capacity;
    size_t size;
  } state{out, n, 0};

  microfmt::sink s{&state, [](void *ctx, std::string_view sv) noexcept {
                     auto *st = static_cast<BoundedBufferState *>(ctx);
                     if (st->data != nullptr && st->size < st->capacity) {
                       const size_t available = st->capacity - st->size;
                       const size_t to_copy =
                           (sv.size() < available) ? sv.size() : available;
                       for (size_t i = 0; i < to_copy; ++i) {
                         st->data[st->size + i] = sv[i];
                       }
                     }
                     st->size += sv.size();
                   }};

  microfmt::format_to(s, fmt_str, args...);

  const size_t written =
      (state.size < state.capacity) ? state.size : state.capacity;
  return {out + written, state.size};
}

// ============================================================================
// fmt::format_to (Iterator / Pointer overload)
// ============================================================================

template <typename OutputIt, typename... Args,
          std::enable_if_t<
              std::is_pointer_v<OutputIt> &&
                  std::is_same_v<
                      std::remove_cv_t<std::remove_pointer_t<OutputIt>>, char>,
              int> = 0>
OutputIt format_to(OutputIt out, std::string_view fmt_str,
                   const Args &...args) noexcept {
  struct PtrSinkState {
    char *ptr;
  } state{out};

  microfmt::sink s{&state, [](void *ctx, std::string_view sv) noexcept {
                     auto *st = static_cast<PtrSinkState *>(ctx);
                     for (char c : sv) {
                       *st->ptr++ = c;
                     }
                   }};

  microfmt::format_to(s, fmt_str, args...);
  return state.ptr;
}

// ============================================================================
// fmt::format (Stack-Bounded Return Type)
// ============================================================================

// Explicit capacity version: fmt::format<256>("...", ...)
template <size_t Capacity, typename... Args>
[[nodiscard]] auto format(std::string_view fmt_str,
                          const Args &...args) noexcept {
  return microfmt::format<Capacity>(fmt_str, args...);
}

// Default capacity version: fmt::format("...", ...)
template <typename... Args>
[[nodiscard]] auto format(std::string_view fmt_str,
                          const Args &...args) noexcept {
  return microfmt::format<DEFAULT_FORMAT_BUFFER_SIZE>(fmt_str, args...);
}

// ============================================================================
// fmt::print & fmt::println
// ============================================================================

inline void stdout_writer(void *, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stdout);
}

inline void stderr_writer(void *, std::string_view sv) noexcept {
  std::fwrite(sv.data(), 1, sv.size(), stderr);
}

template <typename... Args>
void print(std::string_view fmt_str, const Args &...args) noexcept {
  microfmt::sink term{nullptr, stdout_writer};
  microfmt::format_to(term, fmt_str, args...);
}

template <typename... Args>
void print(std::FILE *f, std::string_view fmt_str,
           const Args &...args) noexcept {
  microfmt::sink term{f, [](void *ctx, std::string_view sv) noexcept {
                        std::fwrite(sv.data(), 1, sv.size(),
                                    static_cast<std::FILE *>(ctx));
                      }};
  microfmt::format_to(term, fmt_str, args...);
}

template <typename... Args>
void println(std::string_view fmt_str, const Args &...args) noexcept {
  microfmt::sink term{nullptr, stdout_writer};
  microfmt::format_to(term, fmt_str, args...);
  term.put('\n');
}

template <typename... Args>
void println(std::FILE *f, std::string_view fmt_str,
             const Args &...args) noexcept {
  microfmt::sink term{f, [](void *ctx, std::string_view sv) noexcept {
                        std::fwrite(sv.data(), 1, sv.size(),
                                    static_cast<std::FILE *>(ctx));
                      }};
  microfmt::format_to(term, fmt_str, args...);
  term.put('\n');
}

// ============================================================================
// Range Adapter Bridge
// ============================================================================

template <typename Range>
[[nodiscard]] constexpr auto join(const Range &range,
                                  std::string_view delimiter) noexcept {
  return microfmt::join(range, delimiter);
}

template <typename It, typename Sentinel>
[[nodiscard]] constexpr auto join(It first, Sentinel last,
                                  std::string_view delimiter) noexcept {
  return microfmt::join(first, last, delimiter);
}

} // namespace fmt

// ============================================================================
// microfmt Formatter Bridge
// ============================================================================

namespace microfmt {

namespace detail {

// Checks if fmt::formatter<T> has been specialized
template <typename T, typename = void>
struct has_fmt_formatter : std::false_type {};

template <typename T>
struct has_fmt_formatter<
    T, std::void_t<
           decltype(std::declval<fmt::formatter<T> &>().parse(
               std::declval<fmt::format_parse_context &>())),
           decltype(std::declval<fmt::formatter<T> &>().format(
               std::declval<const T &>(),
               std::declval<fmt::format_context &>()))>> : std::true_type {};

} // namespace detail

// Automatically forward to fmt::formatter<T> when available
template <typename T>
struct formatter<T, std::enable_if_t<detail::has_fmt_formatter<T>::value>> {
  fmt::formatter<T> fmt_impl{};

  constexpr void parse(format_parse_context &ctx) noexcept {
    fmt::format_parse_context pctx{ctx.spec()};
    fmt_impl.parse(pctx);
  }

  void format(const T &val, const sink &out) const noexcept {
    fmt::sink_output_iterator it(out);
    fmt::format_context fctx(it);
    fmt_impl.format(val, fctx);
  }
};

} // namespace microfmt
