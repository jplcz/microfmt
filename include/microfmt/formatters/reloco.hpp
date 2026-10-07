#pragma once
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/floating.hpp>
#include <microfmt/formatters/variant.hpp>
#include <reloco/binary_heap.hpp>
#include <reloco/boxed_slice.hpp>
#include <reloco/call_location.hpp>
#include <reloco/cell.hpp>
#include <reloco/checked.hpp>
#include <reloco/collection_view.hpp>
#include <reloco/cow.hpp>
#include <reloco/duration.hpp>
#include <reloco/error.hpp>
#include <reloco/external_vector.hpp>
#include <reloco/fixed_int.hpp>
#include <reloco/fixed_point.hpp>
#include <reloco/flat_hash_map.hpp>
#include <reloco/flat_hash_set.hpp>
#include <reloco/flat_map.hpp>
#include <reloco/flat_set.hpp>
#include <reloco/inline_flat_map.hpp>
#include <reloco/inline_flat_set.hpp>
#include <reloco/inline_string.hpp>
#include <reloco/inline_vec_deque.hpp>
#include <reloco/inline_vector.hpp>
#include <reloco/instant.hpp>
#include <reloco/lru_cache.hpp>
#include <reloco/masked_pointer.hpp>
#include <reloco/non_zero.hpp>
#include <reloco/obfuscated_string.hpp>
#include <reloco/ordering.hpp>
#include <reloco/outline_vec_deque.hpp>
#include <reloco/outline_vector.hpp>
#include <reloco/packed_bits.hpp>
#include <reloco/rc.hpp>
#include <reloco/ring_buffer.hpp>
#include <reloco/saturating.hpp>
#include <reloco/shared_ptr.hpp>
#include <reloco/sso_flat_map.hpp>
#include <reloco/sso_flat_set.hpp>
#include <reloco/sso_string.hpp>
#include <reloco/sso_vec_deque.hpp>
#include <reloco/sso_vector.hpp>
#include <reloco/string.hpp>
#include <reloco/tree_map.hpp>
#include <reloco/tree_set.hpp>
#include <reloco/type_id.hpp>
#include <reloco/unique_ptr.hpp>
#include <reloco/variant.hpp>
#include <reloco/vec_deque.hpp>
#include <reloco/vector.hpp>
#include <reloco/wrapping.hpp>

namespace microfmt {

namespace detail {

template <typename Formatter, typename Range>
void format_reloco_sequence(const Formatter &elem_formatter, const Range &range, const sink &out) noexcept {
  out.put('[');
  bool is_first = true;
  for (const auto &elem : range) {
    if (!is_first) {
      out.write(", ");
    }
    is_first = false;
    elem_formatter.format(elem, out);
  }
  out.put(']');
}

template <typename KeyFormatter, typename ValueFormatter, typename Range>
void format_reloco_map(const KeyFormatter &key_formatter, const ValueFormatter &value_formatter, const Range &range,
                       const sink &out) noexcept {
  out.put('{');
  bool is_first = true;
  for (const auto &entry : range) {
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

} // namespace detail

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
  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<key_type> key_formatter;
  detail::element_formatter<value_type> value_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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
 * @brief Formatter for `reloco::obfuscated_decrypted_view<N>` -- the type
 * returned by `obfuscated_string<N>::decrypt()`/`RELOCO_OBFUSCATED_STR`.
 *
 * `decrypted_view` already holds its plaintext in a materialized,
 * RAII-wiped buffer (that is its whole purpose), so there is no "no
 * storage" concern here: delegates to the non-template
 * `formatter<microfmt::string_view>`, so the only code instantiated per
 * distinct `N` is this one-line `view()`-and-forward call; the actual
 * width/fill/`{:?}` formatting logic (including debug-quoting) is
 * compiled exactly once and shared across every obfuscated-string length.
 */
template <std::size_t N> struct formatter<reloco::obfuscated_decrypted_view<N>> {
  formatter<microfmt::string_view> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::obfuscated_decrypted_view<N> &val, const sink &out) const noexcept {
    underlying_formatter.format(val.view(), out);
  }
};

/**
 * @brief Formatter for `reloco::obfuscated_string_ref` -- the type-erased
 * handle (see `RELOCO_DECLARE_OBFUSCATED_STR`/`RELOCO_DEFINE_OBFUSCATED_STR`
 * in `obfuscated_string.hpp`).
 *
 * Decodes and writes one byte at a time directly to `out` via
 * `for_each_byte()`: the plaintext is never materialized as a contiguous
 * buffer anywhere, not even transiently -- a stronger guarantee than
 * `formatter<reloco::obfuscated_decrypted_view<N>>` above. This is also
 * the single, non-template formatter `formatter<reloco::obfuscated_string<N>>`
 * below delegates to for every `N`, so no formatting logic is duplicated
 * per obfuscated-string length.
 *
 * Because there is no materialized `string_view` to inspect, this
 * formatter does not support `formatter<microfmt::string_view>`'s `{:?}`
 * debug-quoting flag -- `{}`/`{:?}` both write the raw decoded bytes,
 * exactly like `microfmt`'s own numeric formatters, whose `Debug` and
 * `Display` forms coincide. Reach for `obfuscated_string<N>::decrypt()`
 * directly if quoting is needed.
 */
template <> struct formatter<reloco::obfuscated_string_ref> {
  constexpr void parse(format_parse_context &) const noexcept {}

  void format(reloco::obfuscated_string_ref val, const sink &out) const noexcept {
    val.for_each_byte([&out](char ch) noexcept { out.put(ch); });
  }
};

/**
 * @brief Formatter for `reloco::obfuscated_string<N>` itself.
 *
 * Type-erases to `reloco::obfuscated_string_ref` and delegates to
 * `formatter<reloco::obfuscated_string_ref>` above, so decoding happens
 * byte-by-byte straight to the sink, without ever materializing the full
 * plaintext in a stack buffer -- and this specialization itself adds no
 * further per-`N` formatting logic, only the one-line `as_ref()` forward.
 */
template <std::size_t N> struct formatter<reloco::obfuscated_string<N>> {
  formatter<reloco::obfuscated_string_ref> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::obfuscated_string<N> &val, const sink &out) const noexcept {
    underlying_formatter.format(val.as_ref(), out);
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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

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
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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

  detail::element_formatter<value_type> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

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
  detail::element_formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::non_zero<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::saturating<T>`. Formats the wrapped value.
 */
template <typename T> struct formatter<reloco::saturating<T>> {
  detail::element_formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::saturating<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

/**
 * @brief Formatter for `reloco::wrapping<T>`. Formats the wrapped value.
 */
template <typename T> struct formatter<reloco::wrapping<T>> {
  detail::element_formatter<T> underlying_formatter;

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
  detail::element_formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::checked<T> &value, const sink &out) const noexcept {
    underlying_formatter.format(value.get(), out);
  }
};

// ============================================================================
// Direct Formatters for reloco Time Types (duration.hpp / instant.hpp)
// ============================================================================

/**
 * @brief Formatter for `reloco::duration`.
 *
 * Renders `sec.frac` with a trailing `s` unit, mirroring
 * `formatters/posix_time.hpp`'s `timespec`/`timeval` formatters (which
 * `reloco::duration` shares its `(seconds, subsec_nanoseconds)`
 * representation with). Accepts the same `m`/`3`, `u`/`6`, `n`/`9`
 * fractional-precision suffixes and the `r`/`R` flag to suppress the
 * trailing `s` unit.
 */
template <> struct formatter<reloco::duration> {
  /** @brief Fractional-digit precision (default 9 = nanoseconds). */
  uint8_t precision{9};
  /** @brief Set to `false` (via `r`/`R`) for raw seconds without `s` suffix. */
  bool show_unit{true};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 'm' || c == '3')
        precision = 3;
      else if (c == 'u' || c == '6')
        precision = 6;
      else if (c == 'n' || c == '9')
        precision = 9;
      else if (c == 'r' || c == 'R')
        show_unit = false;
    }
  }

  void format(const reloco::duration &value, const sink &out) const noexcept {
    detail::format_unsigned<detail::radix::decimal>(out, value.as_secs(), false, 0);
    out.put('.');
    if (precision == 3) {
      detail::format_unsigned<detail::radix::decimal>(out, value.subsec_millis(), false, 3);
    } else if (precision == 6) {
      detail::format_unsigned<detail::radix::decimal>(out, value.subsec_micros(), false, 6);
    } else {
      detail::format_unsigned<detail::radix::decimal>(out, value.subsec_nanos(), false, 9);
    }

    if (show_unit) {
      out.put('s');
    }
  }
};

/**
 * @brief Formatter for `reloco::instant`.
 *
 * `instant` carries no defined epoch (see `<reloco/instant.hpp>`'s
 * file-level documentation), so it cannot be rendered as a calendar
 * timestamp the way `formatters/chrono.hpp`'s `system_clock` time-point
 * formatter renders `std::chrono::system_clock::time_point`. Instead this
 * mirrors that same header's `steady_clock` time-point formatter: the
 * value's own elapsed time since the default-constructed ("zero")
 * `instant` -- the closest analog to `steady_clock`'s own opaque,
 * monotonic epoch -- is rendered as `[HH:MM:SS.mmm]` uptime.
 */
template <> struct formatter<reloco::instant> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const reloco::instant &value, const sink &out) const noexcept {
    const reloco::duration since_epoch = value.duration_since(reloco::instant());

    const std::uint64_t total_secs = since_epoch.as_secs();
    const std::uint32_t hours = static_cast<std::uint32_t>(total_secs / 3600);
    const std::uint32_t mins = static_cast<std::uint32_t>((total_secs % 3600) / 60);
    const std::uint32_t secs = static_cast<std::uint32_t>(total_secs % 60);

    detail::format_unsigned<detail::radix::decimal>(out, hours, false, 2);
    out.put(':');
    detail::format_unsigned<detail::radix::decimal>(out, mins, false, 2);
    out.put(':');
    detail::format_unsigned<detail::radix::decimal>(out, secs, false, 2);
    out.put('.');
    detail::format_unsigned<detail::radix::decimal>(out, since_epoch.subsec_millis(), false, 3);
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

/**
 * @brief Formatter for `reloco::error`: the enum member's own name (e.g.
 * `out_of_range`), via a hand-written name table kept in sync with
 * `error.hpp`'s member list, since the enum has no name-lookup helper.
 */
template <> struct formatter<reloco::error> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const reloco::error &val, const sink &out) const noexcept {
    using reloco::error;
    switch (val) {
    case error::allocation_failed:
      out.write("allocation_failed");
      return;
    case error::in_place_growth_failed:
      out.write("in_place_growth_failed");
      return;
    case error::unsupported_operation:
      out.write("unsupported_operation");
      return;
    case error::out_of_range:
      out.write("out_of_range");
      return;
    case error::invalid_argument:
      out.write("invalid_argument");
      return;
    case error::already_exists:
      out.write("already_exists");
      return;
    case error::empty_pointer:
      out.write("empty_pointer");
      return;
    case error::pointer_expired:
      out.write("pointer_expired");
      return;
    case error::no_owner:
      out.write("no_owner");
      return;
    case error::out_of_bounds:
      out.write("out_of_bounds");
      return;
    case error::deadlock:
      out.write("deadlock");
      return;
    case error::invalid_owner:
      out.write("invalid_owner");
      return;
    case error::still_locked:
      out.write("still_locked");
      return;
    case error::not_locked:
      out.write("not_locked");
      return;
    case error::timed_out:
      out.write("timed_out");
      return;
    case error::try_again:
      out.write("try_again");
      return;
    case error::not_initialized:
      out.write("not_initialized");
      return;
    case error::container_empty:
      out.write("container_empty");
      return;
    case error::not_found:
      out.write("not_found");
      return;
    case error::integer_overflow:
      out.write("integer_overflow");
      return;
    case error::division_by_zero:
      out.write("division_by_zero");
      return;
    case error::capacity_exceeded:
      out.write("capacity_exceeded");
      return;
    case error::invalid_state:
      out.write("invalid_state");
      return;
    case error::permission_denied:
      out.write("permission_denied");
      return;
    case error::interrupted:
      out.write("interrupted");
      return;
    case error::resource_exhausted:
      out.write("resource_exhausted");
      return;
    case error::busy:
      out.write("busy");
      return;
    case error::io_error:
      out.write("io_error");
      return;
    case error::operation_canceled:
      out.write("operation_canceled");
      return;
    case error::security_violation:
      out.write("security_violation");
      return;
    case error::page_fault:
      out.write("page_fault");
      return;
    }
    // Defensive fallback for members added to `reloco::error` without a matching case above.
    microfmt::format_to(out, "error({})", static_cast<int>(val));
  }
};

/**
 * @brief Formatter for `reloco::tree_set<T, Compare>`.
 *
 * Formats as a JSON-like array of its (sorted, unique) elements:
 * `[val1, val2, ...]`. Format specifiers cascade down to each element.
 */
template <typename T, typename Compare> struct formatter<reloco::tree_set<T, Compare>> {
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::tree_set<T, Compare> &set, const sink &out) const noexcept {
    detail::format_reloco_sequence(underlying_formatter, set, out);
  }
};

/**
 * @brief Formatter for `reloco::tree_map<Key, Mapped, Compare>`.
 *
 * Formats as a JSON-like object in key order: `{key1: val1, key2: val2, ...}`.
 */
template <typename Key, typename Mapped, typename Compare> struct formatter<reloco::tree_map<Key, Mapped, Compare>> {
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::tree_map<Key, Mapped, Compare> &map, const sink &out) const noexcept {
    detail::format_reloco_map(key_formatter, value_formatter, map, out);
  }
};

/**
 * @brief Formatter for `reloco::lru_cache<Key, Mapped, Hash, KeyEqual>`.
 *
 * Formats as a JSON-like object from most- to least-recently used entry.
 * Does not promote any entry.
 */
template <typename Key, typename Mapped, typename Hash, typename KeyEqual>
struct formatter<reloco::lru_cache<Key, Mapped, Hash, KeyEqual>> {
  detail::element_formatter<std::remove_cv_t<Key>> key_formatter;
  detail::element_formatter<std::remove_cv_t<Mapped>> value_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept {
    key_formatter.parse(ctx);
    value_formatter.parse(ctx);
  }

  void format(const reloco::lru_cache<Key, Mapped, Hash, KeyEqual> &cache, const sink &out) const noexcept {
    detail::format_reloco_map(key_formatter, value_formatter, cache, out);
  }
};

/** @brief Formatter for `reloco::external_vector<T>`: `[val1, val2, ...]`. */
template <typename T> struct formatter<reloco::external_vector<T>> {
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::external_vector<T> &vec, const sink &out) const noexcept {
    detail::format_reloco_sequence(underlying_formatter, vec, out);
  }
};

/**
 * @brief Formatters for the `reloco::ring_buffer` family (`ring_buffer`,
 * `outline_ring_buffer`, `inline_ring_buffer`, `sso_ring_buffer`,
 * `ring_buffer_ref`): `[val1, val2, ...]` from oldest to newest element.
 */
#define MICROFMT_RELOCO_RING_FORMATTER(TEMPLATE_HEAD, TYPE)                                                       \
  TEMPLATE_HEAD struct formatter<TYPE> {                                                                         \
    detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;                                         \
    constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }                \
    void format(const TYPE &ring, const sink &out) const noexcept {                                              \
      detail::format_reloco_sequence(underlying_formatter, ring, out);                                           \
    }                                                                                                            \
  }

#define MICROFMT_COMMA ,
MICROFMT_RELOCO_RING_FORMATTER(template <typename T>, reloco::ring_buffer<T>);
MICROFMT_RELOCO_RING_FORMATTER(template <typename T>, reloco::outline_ring_buffer<T>);
MICROFMT_RELOCO_RING_FORMATTER(template <typename T>, reloco::ring_buffer_ref<T>);
MICROFMT_RELOCO_RING_FORMATTER(template <typename T MICROFMT_COMMA std::size_t N>,
                               reloco::inline_ring_buffer<T MICROFMT_COMMA N>);
MICROFMT_RELOCO_RING_FORMATTER(template <typename T MICROFMT_COMMA std::size_t N>,
                               reloco::sso_ring_buffer<T MICROFMT_COMMA N>);
#undef MICROFMT_COMMA
#undef MICROFMT_RELOCO_RING_FORMATTER

/** @brief Formatter for `reloco::variant<Ts...>`: formats the active alternative like `std::variant`. */
template <typename... Ts> struct formatter<reloco::variant<Ts...>> {
  formatter<std::variant<Ts...>> base_formatter; // std-interop-ok: reloco::variant derives from std::variant

  constexpr void parse(format_parse_context &ctx) noexcept { base_formatter.parse(ctx); }

  void format(const reloco::variant<Ts...> &var, const sink &out) const noexcept {
    base_formatter.format(static_cast<const std::variant<Ts...> &>(var), out); // std-interop-ok: base-class view
  }
};

/** @brief Formatter for `reloco::cell<T>`: formats the current value. */
template <typename T> struct formatter<reloco::cell<T>> {
  detail::element_formatter<std::remove_cv_t<T>> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::cell<T> &c, const sink &out) const noexcept { underlying_formatter.format(c.get(), out); }
};

/** @brief Formatter for `reloco::call_location`: `file:line`. */
template <> struct formatter<reloco::call_location> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const reloco::call_location &loc, const sink &out) const noexcept {
    out.write(loc.file != nullptr ? microfmt::string_view(loc.file) : microfmt::string_view("<unknown>"));
    out.put(':');
    microfmt::format_to(out, "{}", loc.line);
  }
};

/** @brief Formatter for `reloco::call_location_ref`: `file:line`, or `<no location>` when empty. */
template <> struct formatter<reloco::call_location_ref> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const reloco::call_location_ref &ref, const sink &out) const noexcept {
    if (!ref.has_value()) {
      out.write("<no location>");
      return;
    }
    formatter<reloco::call_location>{}.format(ref.value(), out);
  }
};

/** @brief Formatter for `reloco::packed_bits<T>`: the raw packed integer; specifiers apply to it. */
template <typename T> struct formatter<reloco::packed_bits<T>> {
  formatter<T> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::packed_bits<T> &bits, const sink &out) const noexcept {
    underlying_formatter.format(bits.value(), out);
  }
};

/**
 * @brief Formatter for `reloco::masked_pointer<T, AuthPolicy>`: the authenticated pointer address
 * (`0x...`), or `(null)`. Authenticating a corrupted pointer follows the policy's own failure behavior.
 */
template <typename T, typename AuthPolicy> struct formatter<reloco::masked_pointer<T, AuthPolicy>> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const reloco::masked_pointer<T, AuthPolicy> &ptr, const sink &out) const noexcept {
    const void *raw = static_cast<const void *>(ptr.get());
    if (raw == nullptr) {
      out.write("(null)");
      return;
    }
    microfmt::format_to(out, "{}", raw);
  }
};

/**
 * @brief Formatter for `reloco::fixed_point<Rep, FracBits>` with a built-in integral `Rep`.
 *
 * Formats the value as a decimal number; the specifier is forwarded to `double`'s formatter
 * (e.g. `{:.3f}`).
 */
template <typename Rep, unsigned FracBits>
struct formatter<reloco::fixed_point<Rep, FracBits>, std::enable_if_t<std::is_arithmetic_v<Rep>>> {
  formatter<double> underlying_formatter;

  constexpr void parse(format_parse_context &ctx) noexcept { underlying_formatter.parse(ctx); }

  void format(const reloco::fixed_point<Rep, FracBits> &value, const sink &out) const noexcept {
    underlying_formatter.format(static_cast<double>(value.raw()) / static_cast<double>(Rep{1} << FracBits), out);
  }
};

/**
 * @brief Formatter for `reloco::detail::wide_int<N, Signed>` (the `reloco::fixed_int<N, Signed>` fallback for
 * widths beyond the native integers).
 *
 * Supports the same specifiers as the built-in integer formatter: width, `0` padding, `x`/`X` hex and `#`.
 */
template <std::size_t N, bool Signed>
struct formatter<reloco::detail::wide_int<N, Signed>> : detail::int_formatter_specs {
  void format(const reloco::detail::wide_int<N, Signed> &val, const sink &out) const noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    constexpr std::size_t limb_count = N / 32;
    // Enough for N/4 hex digits or ceil(N * log10(2)) decimal digits.
    constexpr std::size_t buf_size = N / 3 + 2;

    std::uint32_t mag[limb_count];
    const bool negative = val.is_negative();
    const reloco::detail::wide_int<N, Signed> abs_val = negative ? -val : val;
    for (std::size_t i = 0; i < limb_count; ++i) {
      mag[i] = abs_val.limb(i);
    }

    char buffer[buf_size];
    char *end = buffer + buf_size;
    char *start = end;

    microfmt::string_view prefix{};
    if (flags.is_hex) {
      const char *lut = flags.uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
      for (std::size_t i = 0; i < limb_count; ++i) {
        std::uint32_t limb = mag[i];
        for (int nibble = 0; nibble < 8; ++nibble) {
          *--start = lut[limb & 0xFU];
          limb >>= 4;
        }
      }
      // Drop leading zeros, keeping at least one digit.
      while (start < end - 1 && *start == '0') {
        ++start;
      }
      if (flags.alt_form) {
        prefix = flags.uppercase ? microfmt::string_view("0X") : microfmt::string_view("0x");
      }
    } else {
      std::size_t used = limb_count;
      do {
        // Divide the magnitude by 10^9 in place, emitting the 9-digit remainder.
        std::uint64_t rem = 0;
        for (std::size_t i = used; i-- > 0;) {
          const std::uint64_t cur = (rem << 32) | mag[i];
          mag[i] = static_cast<std::uint32_t>(cur / 1000000000U);
          rem = cur % 1000000000U;
        }
        while (used > 0 && mag[used - 1] == 0) {
          --used;
        }
        auto chunk = static_cast<std::uint32_t>(rem);
        for (int digit = 0; digit < 9 && (used > 0 || chunk != 0 || digit == 0); ++digit) {
          *--start = static_cast<char>('0' + chunk % 10U);
          chunk /= 10U;
        }
      } while (used > 0);
    }

    detail::emit_formatted_int(out, start, static_cast<std::size_t>(end - start), negative, prefix, width,
                               flags.zero_pad);
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
};

#if defined(__SIZEOF_INT128__)
/**
 * @brief Formatter for `__int128`/`unsigned __int128` when they are not `std::is_integral` (strict `-std=c++NN`),
 * i.e. `reloco::fixed_int<128, S>` there; forwards to the `wide_int` formatter.
 */
template <typename T>
struct formatter<T, std::enable_if_t<reloco::detail::is_builtin_int128_v<T> && !std::is_integral_v<T>>>
    : formatter<reloco::detail::wide_int<128, std::is_same_v<T, __int128>>> {
  void format(T val, const sink &out) const noexcept {
    constexpr bool is_signed = std::is_same_v<T, __int128>;
    formatter<reloco::detail::wide_int<128, is_signed>>::format(reloco::detail::wide_int<128, is_signed>(val), out);
  }
};
#endif

} // namespace microfmt
