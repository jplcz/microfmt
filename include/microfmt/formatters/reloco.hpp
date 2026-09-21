#pragma once
#include <microfmt/microfmt.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/string.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/vector.hpp>

namespace microfmt {

/**
 * @brief Core formatter for type-erased collection views.
 *
 * Instantiated exactly once per element type `T`. Because it formats
 * through `collection_view`'s vtable, it natively handles bounds-checking
 * and data access for *any* underlying container without template bloat,
 * reusing the same instruction cache for vector<T>, span<T>, array<T, N>.
 */
template <typename T> struct formatter<reloco::collection_view<T>> {
  using value_type = std::remove_cv_t<T>;

  // Holds the parsed state (e.g., width, hex flags) for the underlying elements
  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    // Delegate parsing of the format specifier to the element formatter.
    // e.g., "{:04x}" parsed here will configure `underlying_formatter` to pad hex.
    underlying_formatter.parse(ctx);
  }

  void format(const reloco::collection_view<T> &view, const sink &out) const noexcept {
    out.put('[');

    bool is_first = true;

    // Type-erased visit across the container's elements
    view.for_each([&](const T &elem) noexcept {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;

      // Delegate formatting to the pre-configured element formatter
      underlying_formatter.format(elem, out);
    });

    out.put(']');
  }
};

/**
 * @brief Explicit adapter to format any reloco-adapted container.
 *
 * @example
 *   reloco::vector<int> my_vec = ...;
 *   microfmt::format("{}", microfmt::as_collection_view(my_vec));
 */
template <typename Container,
          std::enable_if_t<reloco::detail::has_collection_view_traits<std::remove_const_t<Container>>::value, int> = 0>
constexpr auto as_collection_view(Container &c) noexcept {
  using traits = reloco::collection_view_traits<std::remove_const_t<Container>>;
  using element_type = typename traits::element_type;

  // Force a const view for formatting so it can safely bind to both const and non-const containers
  return reloco::collection_view<const element_type>(c);
}

/**
 * @brief Formatter for sequence container references (e.g. vector adapter).
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`
 * Format specifiers (e.g. `{:04X}`) cascade down to the elements.
 */
template <typename T> struct formatter<reloco::detail::mutable_sequence_container_ref<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::detail::mutable_sequence_container_ref<T> &cref, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;

    cref.for_each([&](T &elem) noexcept {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      underlying_formatter.format(elem, out);
    });

    out.put(']');
  }
};

/**
 * @brief Formatter for associative container references (e.g. flat_set, map adapters).
 *
 * Formats as a JSON-like object: `{key1: val1, key2: val2, ...}`
 * Format specifiers cascade down to the **values**, not the keys, which matches
 * standard telemetry/structured logging expectations.
 */
template <typename T, typename Key> struct formatter<reloco::detail::mutable_associative_container_ref<T, Key>> {
  using value_type = std::remove_cv_t<T>;
  using key_type = std::remove_cv_t<Key>;

  formatter<key_type> key_formatter;
  formatter<value_type> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    // Apply parsed specs (like hex formatting) to the values
    value_formatter.parse(ctx);
  }

  void format(const reloco::detail::mutable_associative_container_ref<T, Key> &cref, const sink &out) const noexcept {
    out.put('{');
    bool is_first = true;

    cref.for_each([&](const Key &key, T &value) noexcept {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;

      key_formatter.format(key, out);
      out.write(": ");
      value_formatter.format(value, out);
    });

    out.put('}');
  }
};

// ============================================================================
// Direct Formatters for Concrete reloco Containers
// ============================================================================
//
// The `collection_view`/`container_ref` formatters above are a type-erased
// *fallback* -- they let one formatter instantiation serve any adapted
// container, at the cost of an explicit `as_collection_view(...)` call at
// each use site. The formatters below are the "normal" path: they let
// `microfmt::format("{}", my_vec)` work directly on `reloco::vector<T>` and
// friends, with no wrapper call needed.

/**
 * @brief Formatter for `reloco::vector<T>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`. Format specifiers cascade
 * down to each element.
 */
template <typename T> struct formatter<reloco::vector<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::vector<T> &vec, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : vec) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      underlying_formatter.format(elem, out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::flat_set<T, Compare>`.
 *
 * Formats as a JSON-like array of its (sorted, unique) elements:
 * `[val1, val2, ...]`. Format specifiers cascade down to each element.
 */
template <typename T, typename Compare> struct formatter<reloco::flat_set<T, Compare>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::flat_set<T, Compare> &set, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : set) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      underlying_formatter.format(elem, out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::basic_string<CharT, TraitsT>`.
 *
 * Writes the string's characters directly, the same way
 * `microfmt::string_view` (a `reloco::basic_string_view` alias) is formatted.
 */
template <typename CharT, typename TraitsT> struct formatter<reloco::basic_string<CharT, TraitsT>> {
  formatter<reloco::basic_string_view<CharT, TraitsT>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::basic_string<CharT, TraitsT> &str, const sink &out) const noexcept {
    underlying_formatter.format(str.view(), out);
  }
};

/**
 * @brief Formatter for `reloco::basic_inline_string<Capacity, CharT, TraitsT>`.
 *
 * Writes the string's characters directly, the same way
 * `microfmt::string_view` (a `reloco::basic_string_view` alias) is formatted.
 */
template <std::size_t Capacity, typename CharT, typename TraitsT>
struct formatter<reloco::basic_inline_string<Capacity, CharT, TraitsT>> {
  formatter<reloco::basic_string_view<CharT, TraitsT>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::basic_inline_string<Capacity, CharT, TraitsT> &str, const sink &out) const noexcept {
    underlying_formatter.format(str.view(), out);
  }
};

/**
 * @brief Formatter for `reloco::basic_sso_string<CharT, TraitsT>`.
 *
 * Writes the string's characters directly, the same way
 * `microfmt::string_view` (a `reloco::basic_string_view` alias) is formatted.
 */
template <typename CharT, typename TraitsT> struct formatter<reloco::basic_sso_string<CharT, TraitsT>> {
  formatter<reloco::basic_string_view<CharT, TraitsT>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::basic_sso_string<CharT, TraitsT> &str, const sink &out) const noexcept {
    underlying_formatter.format(str.view(), out);
  }
};

/**
 * @brief Formatter for `reloco::inline_vector<T, Capacity>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`. Format specifiers cascade
 * down to each element.
 */
template <typename T, std::size_t Capacity> struct formatter<reloco::inline_vector<T, Capacity>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::inline_vector<T, Capacity> &vec, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : vec) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      underlying_formatter.format(elem, out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::inline_flat_set<T, Capacity, Compare>`.
 *
 * Formats as a JSON-like array of its (sorted, unique) elements:
 * `[val1, val2, ...]`. Format specifiers cascade down to each element.
 */
template <typename T, std::size_t Capacity, typename Compare>
struct formatter<reloco::inline_flat_set<T, Capacity, Compare>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::inline_flat_set<T, Capacity, Compare> &set, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : set) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;
      underlying_formatter.format(elem, out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::flat_map<Key, Mapped, Compare>`.
 *
 * Formats as a JSON-like object: `{key1: val1, key2: val2, ...}`. Format
 * specifiers cascade down to both the keys and the values.
 */
template <typename Key, typename Mapped, typename Compare> struct formatter<reloco::flat_map<Key, Mapped, Compare>> {
  formatter<std::remove_cv_t<Key>> key_formatter;
  formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::flat_map<Key, Mapped, Compare> &map, const sink &out) const noexcept {
    out.put('{');
    bool is_first = true;
    for (const auto &entry : map) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;

      key_formatter.format(entry.first, out);
      out.write(": ");
      value_formatter.format(entry.second, out);
    }
    out.put('}');
  }
};

/**
 * @brief Formatter for `reloco::inline_flat_map<Key, Mapped, Capacity,
 * Compare>`.
 *
 * Formats as a JSON-like object: `{key1: val1, key2: val2, ...}`. Format
 * specifiers cascade down to both the keys and the values.
 */
template <typename Key, typename Mapped, std::size_t Capacity, typename Compare>
struct formatter<reloco::inline_flat_map<Key, Mapped, Capacity, Compare>> {
  formatter<std::remove_cv_t<Key>> key_formatter;
  formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::inline_flat_map<Key, Mapped, Capacity, Compare> &map, const sink &out) const noexcept {
    out.put('{');
    bool is_first = true;
    for (const auto &entry : map) {
      if (!is_first) {
        out.write(", ");
      }
      is_first = false;

      key_formatter.format(entry.first, out);
      out.write(": ");
      value_formatter.format(entry.second, out);
    }
    out.put('}');
  }
};

// ============================================================================
// Direct Formatters for reloco Pointer/Borrow Wrappers
// ============================================================================
//
// These format transparently: the pointee's value, not the wrapper's address
// (unlike the GDB pretty printers, which show addresses -- see
// docs/gdb-pretty-printers.md -- since debugging memory layout and logging a
// value are different goals). Each requires `formatter<T>` for the pointee
// type `T`.

/**
 * @brief Formatter for `microfmt::value_ptr<T>` (a `reloco::value_ptr<T>`
 * alias).
 *
 * Formats the pointee's value directly, or the literal text `(null)` when
 * empty.
 */
template <typename T> struct formatter<value_ptr<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const value_ptr<T> &ptr, const sink &out) const noexcept {
    if (!ptr) {
      out.write("(null)");
      return;
    }
    underlying_formatter.format(*ptr.get(), out);
  }
};

/**
 * @brief Formatter for `microfmt::value_ref<T>` (a `reloco::value_ref<T>`
 * alias).
 *
 * Always non-null by construction, so this formats the referenced value
 * directly with no null case.
 */
template <typename T> struct formatter<value_ref<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const value_ref<T> &ref, const sink &out) const noexcept { underlying_formatter.format(*ref, out); }
};

/**
 * @brief Formatter for `reloco::unique_ptr<T>`.
 *
 * Formats the pointee's value directly, or the literal text `(null)` when
 * empty.
 */
template <typename T> struct formatter<reloco::unique_ptr<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::unique_ptr<T> &ptr, const sink &out) const noexcept {
    if (!ptr) {
      out.write("(null)");
      return;
    }
    underlying_formatter.format(*ptr.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::shared_ptr<T>`.
 *
 * Formats the pointee's value directly, or the literal text `(null)` when
 * empty.
 */
template <typename T> struct formatter<reloco::shared_ptr<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::shared_ptr<T> &ptr, const sink &out) const noexcept {
    if (!ptr) {
      out.write("(null)");
      return;
    }
    underlying_formatter.format(*ptr.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::weak_ptr<T>`.
 *
 * Attempts to `lock()` the referenced object: formats its value directly if
 * still alive, or the literal text `(expired)` once the last owning
 * `shared_ptr` has released it.
 */
template <typename T> struct formatter<reloco::weak_ptr<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::weak_ptr<T> &weak, const sink &out) const noexcept {
    auto locked = weak.lock();
    if (!locked.has_value()) {
      out.write("(expired)");
      return;
    }
    underlying_formatter.format(*locked.value().get(), out);
  }
};

} // namespace microfmt

