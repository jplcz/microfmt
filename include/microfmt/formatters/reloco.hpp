#pragma once
#include <microfmt/microfmt.hpp>
#include <reloco/binary_heap.hpp>
#include <reloco/boxed_slice.hpp>
#include <reloco/checked.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/cow.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vec_deque.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/non_zero.hpp>
#include <reloco/ordering.hpp>
#include <reloco/outline_vec_deque.hpp>
#include <reloco/outline_vector.hpp>
#include <reloco/rc.hpp>
#include <reloco/saturating.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/sso_flat_map.hpp>
#include <reloco/sso_flat_set.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/sso_vec_deque.hpp>
#include <reloco/sso_vector.hpp>
#include <reloco/string.hpp>
#include <reloco/type_id.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/vec_deque.hpp>
#include <reloco/vector.hpp>
#include <reloco/wrapping.hpp>

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

/**
 * @brief Formatter for `reloco::boxed_slice<T>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`. Format specifiers
 * cascade down to each element.
 */
template <typename T> struct formatter<reloco::boxed_slice<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::boxed_slice<T> &slice, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : slice) {
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
 * @brief Formatter for `reloco::binary_heap<T, Compare>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`, in the heap's
 * unspecified internal order (not sorted priority order -- matching
 * `begin()`/`end()`'s own documented behavior). Format specifiers cascade
 * down to each element.
 */
template <typename T, typename Compare> struct formatter<reloco::binary_heap<T, Compare>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::binary_heap<T, Compare> &heap, const sink &out) const noexcept {
    out.put('[');
    bool is_first = true;
    for (const auto &elem : heap) {
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
 * @brief Formatter for `reloco::sso_vector<T, InlineCapacity>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`. Format specifiers
 * cascade down to each element.
 */
template <typename T, std::size_t InlineCapacity> struct formatter<reloco::sso_vector<T, InlineCapacity>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::sso_vector<T, InlineCapacity> &vec, const sink &out) const noexcept {
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
 * @brief Formatter for `reloco::sso_flat_set<T, InlineCapacity, Compare>`.
 *
 * Formats as a JSON-like array of its (sorted, unique) elements:
 * `[val1, val2, ...]`. Format specifiers cascade down to each element.
 */
template <typename T, std::size_t InlineCapacity, typename Compare>
struct formatter<reloco::sso_flat_set<T, InlineCapacity, Compare>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::sso_flat_set<T, InlineCapacity, Compare> &set, const sink &out) const noexcept {
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
 * @brief Formatter for `reloco::sso_flat_map<Key, Mapped, InlineCapacity,
 * Compare>`.
 *
 * Formats as a JSON-like object: `{key1: val1, key2: val2, ...}`. Format
 * specifiers cascade down to both the keys and the values.
 */
template <typename Key, typename Mapped, std::size_t InlineCapacity, typename Compare>
struct formatter<reloco::sso_flat_map<Key, Mapped, InlineCapacity, Compare>> {
  formatter<std::remove_cv_t<Key>> key_formatter;
  formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::sso_flat_map<Key, Mapped, InlineCapacity, Compare> &map, const sink &out) const noexcept {
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
 * @brief Formatter for `reloco::outline_vector<T>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`. Format specifiers
 * cascade down to each element.
 */
template <typename T> struct formatter<reloco::outline_vector<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::outline_vector<T> &vec, const sink &out) const noexcept {
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
 * @brief Formatter for `reloco::vec_deque<T>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`, front to back. Format
 * specifiers cascade down to each element. Iterates by index
 * (`operator[]`/`size()`) rather than `begin()`/`end()`: the deque's
 * physically-wrapping ring-buffer storage means it has no contiguous
 * iterator to offer (see `collection_view_traits<vec_deque<T>>::has_data`).
 */
template <typename T> struct formatter<reloco::vec_deque<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::vec_deque<T> &deque, const sink &out) const noexcept {
    out.put('[');
    const std::size_t n = deque.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        out.write(", ");
      }
      underlying_formatter.format(deque[i], out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::sso_vec_deque<T, InlineCapacity>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`, front to back. Format
 * specifiers cascade down to each element. Iterates by index, same
 * rationale as the `vec_deque<T>` formatter above.
 */
template <typename T, std::size_t InlineCapacity> struct formatter<reloco::sso_vec_deque<T, InlineCapacity>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::sso_vec_deque<T, InlineCapacity> &deque, const sink &out) const noexcept {
    out.put('[');
    const std::size_t n = deque.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        out.write(", ");
      }
      underlying_formatter.format(deque[i], out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::inline_vec_deque<T, Capacity>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`, front to back. Format
 * specifiers cascade down to each element. Iterates by index, same
 * rationale as the `vec_deque<T>` formatter above.
 */
template <typename T, std::size_t Capacity> struct formatter<reloco::inline_vec_deque<T, Capacity>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::inline_vec_deque<T, Capacity> &deque, const sink &out) const noexcept {
    out.put('[');
    const std::size_t n = deque.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        out.write(", ");
      }
      underlying_formatter.format(deque[i], out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::outline_vec_deque<T>`.
 *
 * Formats as a JSON-like array: `[val1, val2, ...]`, front to back. Format
 * specifiers cascade down to each element. Iterates by index, same
 * rationale as the `vec_deque<T>` formatter above.
 */
template <typename T> struct formatter<reloco::outline_vec_deque<T>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::outline_vec_deque<T> &deque, const sink &out) const noexcept {
    out.put('[');
    const std::size_t n = deque.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (i != 0) {
        out.write(", ");
      }
      underlying_formatter.format(deque[i], out);
    }
    out.put(']');
  }
};

/**
 * @brief Formatter for `reloco::flat_hash_set<T, Hash, KeyEqual>`.
 *
 * Formats as a JSON-like array of its (unique) elements: `[val1, val2,
 * ...]`, in the hash table's unspecified internal bucket order (not
 * insertion or sorted order -- matching `begin()`/`end()`'s own documented
 * behavior, the same caveat `binary_heap`'s formatter documents). Format
 * specifiers cascade down to each element.
 */
template <typename T, typename Hash, typename KeyEqual> struct formatter<reloco::flat_hash_set<T, Hash, KeyEqual>> {
  using value_type = std::remove_cv_t<T>;

  formatter<value_type> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::flat_hash_set<T, Hash, KeyEqual> &set, const sink &out) const noexcept {
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
 * @brief Formatter for `reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual>`.
 *
 * Formats as a JSON-like object: `{key1: val1, key2: val2, ...}`, in the
 * hash table's unspecified internal bucket order (see the `flat_hash_set`
 * formatter's own doc comment). Format specifiers cascade down to both the
 * keys and the values.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct formatter<reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual>> {
  formatter<std::remove_cv_t<Key>> key_formatter;
  formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::flat_hash_map<Key, Mapped, Hash, KeyEqual> &map, const sink &out) const noexcept {
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

/**
 * @brief Formatter for `reloco::rc<T>`.
 *
 * Structurally identical to `shared_ptr<T>`'s formatter: formats the
 * pointee's value directly, or the literal text `(null)` when empty.
 */
template <typename T> struct formatter<reloco::rc<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::rc<T> &ptr, const sink &out) const noexcept {
    if (!ptr) {
      out.write("(null)");
      return;
    }
    underlying_formatter.format(*ptr.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::weak_rc<T>`.
 *
 * Attempts to `lock()` the referenced object: formats its value directly if
 * still alive, or the literal text `(expired)` once the last owning `rc`
 * has released it.
 */
template <typename T> struct formatter<reloco::weak_rc<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::weak_rc<T> &weak, const sink &out) const noexcept {
    auto locked = weak.lock();
    if (!locked.has_value()) {
      out.write("(expired)");
      return;
    }
    underlying_formatter.format(*locked.value().get(), out);
  }
};

/**
 * @brief Formatter for `reloco::cow<T>`.
 *
 * Formats the current value directly (borrowed or owned, transparently --
 * see `cow<T>::get()`); no wrapper-specific decoration, since a `cow<T>`
 * always holds a valid `T` once constructed.
 */
template <typename T> struct formatter<reloco::cow<T>> {
  formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::cow<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

// ============================================================================
// Direct Formatters for reloco Numeric Newtypes
// ============================================================================
//
// `non_zero<T>`/`saturating<T>`/`wrapping<T>`/`checked<T>` all implicitly
// convert to their wrapped `T`, but formatter lookup is keyed on the exact
// argument type, so each still needs its own (trivial) specialization
// forwarding to `formatter<T>`.

/**
 * @brief Formatter for `reloco::non_zero<T>`. Formats the wrapped value.
 */
template <typename T> struct formatter<reloco::non_zero<T>> {
  formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::non_zero<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::saturating<T>`. Formats the wrapped value.
 */
template <typename T> struct formatter<reloco::saturating<T>> {
  formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::saturating<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::wrapping<T>`. Formats the wrapped value.
 */
template <typename T> struct formatter<reloco::wrapping<T>> {
  formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::wrapping<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::checked<T>`. Formats the wrapped value.
 *
 * Distinct from `microfmt::checked_value<T>` (a `reloco::checked_value<T>`
 * alias, formatted in `formatters/monad.hpp`): `checked<T>` wraps a plain
 * integral `T` with fallible `try_add`/`try_sub`/... arithmetic, whereas
 * `checked_value<T>` propagates a sticky first-error state through
 * unchecked-looking operator overloads.
 */
template <typename T> struct formatter<reloco::checked<T>> {
  formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::checked<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::ordering`.
 *
 * Writes one of the literal texts `less`, `equal`, `greater`, matching
 * Rust's `Debug` output for `std::cmp::Ordering`.
 */
template <> struct formatter<reloco::ordering> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(reloco::ordering value, const sink &out) const noexcept {
    switch (value) {
    case reloco::ordering::less:
      out.write("less");
      return;
    case reloco::ordering::equal:
      out.write("equal");
      return;
    case reloco::ordering::greater:
      out.write("greater");
      return;
    }
    out.write("unknown");
  }
};

/**
 * @brief Formatter for `reloco::type_id`, the RTTI-free process-wide type
 * identity from `<reloco/type_id.hpp>`.
 *
 * Writes the debug name registered for the type via `RELOCO_TYPE_ID_NAME`
 * (or the `RELOCO_IMPLICIT_TYPEID` `typeid(T).name()` fallback, if the
 * consumer opted into it), falling back to `<unnamed type>` when no name
 * is available, and to `<no type>` for the default-constructed ("no
 * type") sentinel. The identity itself (`type_id`'s underlying tag
 * pointer) is never printed: it is an opaque, process-local value with no
 * meaningful textual representation across runs.
 */
template <> struct formatter<reloco::type_id> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(reloco::type_id value, const sink &out) const noexcept {
    if (!value) {
      out.write("<no type>");
      return;
    }
    const char *name = value.name();
    if (name == nullptr) {
      out.write("<unnamed type>");
      return;
    }
    out.write(name);
  }
};

} // namespace microfmt

