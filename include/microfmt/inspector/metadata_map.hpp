// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file metadata_map.hpp
 * @brief Type-erased, zero-allocation metadata map generator view. */

#include "../reloco.hpp"
#include "../markdown.hpp"
#include "../microfmt.hpp"

namespace microfmt {

/**
 * @brief Function pointer signature for type-erased property value formatting.
 */
using property_print_fn_t = void (*)(const void *val_ptr, const sink &out) noexcept;

/**
 * @brief Type-erased property entry synthesized during generation.
 */
struct RELOCO_POINTER property_entry {
  microfmt::string_view key{};
  value_ptr<const void> val_ptr{};
  property_print_fn_t print_fn{nullptr};

  /**
   * @brief Formats the underlying property value using its type-erased thunk.
   */
  void format_value(const sink &out) const noexcept {
    if (print_fn && val_ptr) {
      print_fn(val_ptr.get(), out);
    } else {
      out.write("null");
    }
  }
};

/**
 * @brief Function pointer signature for fetching the next property entry.
 *
 * @param ctx Mutable pointer to the caller's stack-allocated iteration state.
 * @param out Output property_entry to populate.
 * @return `true` if a property was successfully populated, `false` when iteration is complete.
 */
using metadata_next_fn_t = bool (*)(void *ctx, property_entry &out) noexcept;

/**
 * @brief Type-erased, non-owning metadata map view driven by a sequential next callback.
 *
 * Designed for extreme low-stack environments by avoiding heavy C++ iterator state machines
 * in favor of a direct generator pattern (`get_next`).
 */
class RELOCO_POINTER metadata_map {
public:
  /**
   * @brief Constructs a metadata_map view over an iteration context and a next function.
   *
   * @param ctx Mutable pointer to caller-owned stack state.
   * @param next_fn Function pointer that populates the next property and advances state.
   */
  constexpr metadata_map(
      void *ctx RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS,
                         metadata_next_fn_t next_fn) noexcept
      : m_ctx(ctx), m_next_fn(next_fn) {}

  /**
   * @brief Fetches the next property entry, advancing the internal generator state.
   * @param out Reference to populate with the next property_entry.
   * @return `true` if an entry was populated, `false` when iteration ends.
   */
  [[nodiscard]] constexpr bool get_next(property_entry &out) const noexcept {
    if (m_next_fn && m_ctx) {
      return m_next_fn(m_ctx.get(), out);
    }
    return false;
  }

private:
  value_ptr<void> m_ctx{};
  metadata_next_fn_t m_next_fn{nullptr};
};

} // namespace microfmt

// ============================================================================
// Formatter Specialization for metadata_map (Zero-Overhead while Loop)
// ============================================================================

namespace microfmt {

template <> struct formatter<microfmt::metadata_map> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const microfmt::metadata_map &map, const sink &out) const noexcept {
    out.put('{');
    property_entry entry;
    std::size_t idx = 0;

    // Direct generator loop minimizing local stack variables and branch overhead
    while (map.get_next(entry)) {
      if (idx > 0) {
        out.write(", ");
      }
      microfmt::format_to(out, MICROFMT_STRING("\"{}\": "), entry.key);
      entry.format_value(out);
      ++idx;
    }
    out.put('}');
  }
};

} // namespace microfmt

namespace microfmt::md {

/**
 * @brief Streams any type-erased metadata_map view into a Markdown table.
 *
 * @param w Markdown writer instance.
 * @param map Type-erased metadata_map view.
 * @param scratch_buf Caller-provided character span used as scratch memory for formatting values.
 * @param title Optional table header title.
 */
inline void write_metadata_table(writer &w, metadata_map map, span<char> scratch_buf,
                                 string_view title = "Metadata Properties") noexcept {
  static constexpr column cols[2] = {{"Property", 12, align::left}, {"Value", 16, align::left}};

  w.h3(title);

  table_writer<2> tw(w, cols);

  // Initialize the caller-provided span_sink scratchpad
  span_sink scratch(scratch_buf);

  property_entry entry;
  while (map.get_next(entry)) {
    scratch.reset();                       // Clear workspace for the next row
    entry.format_value(scratch.as_sink()); // Format into scratch buffer

    tw.row(entry.key, scratch.view());
  }
}

} // namespace microfmt::md