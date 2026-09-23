// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file foreign_string_view.hpp @brief Type-erased foreign string view
 * formatter for microfmt. */

#include "../microfmt.hpp"
#include "address_space.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace microfmt {

// ============================================================================
// Foreign String View
// ============================================================================

/**
 * @brief Streaming view of a foreign or non-contiguous string.
 *
 * Pulls a string from a custom source or foreign address space through
 * a reusable scratch buffer in bounded chunks.
 */
class MICROFMT_API_CLASS RELOCO_POINTER foreign_string_view {
public:
  /**
   * @brief Constructs an empty (null) foreign string view.
   */
  constexpr foreign_string_view() noexcept = default;

  /**
   * @brief Constructs a view over a foreign string using an address space and
   * address.
   * @param addr Absolute address or identifier of the foreign string.
   * @param space Address space handle used to read the string chunks.
   * @param scratch Reusable chunk-read buffer.
   * @param max_limit Maximum characters to render.
   */
  constexpr foreign_string_view(uintptr_t addr, address_space_ref space,
                                span<char> scratch RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS,
                                size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch), max_limit_(max_limit) {}

  /**
   * @brief Constructs a view over a foreign string using a fixed C array as
   * scratch.
   * @tparam N Size of the scratch array.
   * @param addr Absolute address or identifier of the foreign string.
   * @param space Address space handle used to read the string chunks.
   * @param scratch Reusable chunk-read buffer.
   * @param max_limit Maximum characters to render.
   */
  template <size_t N>
  constexpr foreign_string_view(uintptr_t addr, address_space_ref space,
                                char (&scratch RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS)[N],
                                size_t max_limit = 4096) noexcept
      : addr_(addr), space_(space), scratch_(scratch, N), max_limit_(max_limit) {}

  /**
   * @brief Returns the foreign string address.
   * @return Absolute address, or `0` when null.
   */
  [[nodiscard]] constexpr uintptr_t address() const noexcept { return addr_; }

  /**
   * @brief Returns the address space handle.
   * @return Bound @ref address_space_ref.
   */
  [[nodiscard]] constexpr address_space_ref space() const noexcept { return space_; }

  /**
   * @brief Returns the scratch chunk buffer.
   * @return Scratch span used for chunked reads.
   */
  [[nodiscard]] constexpr span<char> scratch() const noexcept RELOCO_LIFETIMEBOUND { return scratch_; }

  /**
   * @brief Returns the maximum render length.
   * @return Character limit.
   */
  [[nodiscard]] constexpr size_t max_limit() const noexcept { return max_limit_; }

private:
  /// Foreign string address.
  uintptr_t addr_{0};
  /// Address space handle.
  address_space_ref space_{};
  /// Scratch chunk buffer.
  span<char> scratch_{};
  /// Maximum render length.
  size_t max_limit_{4096};
};

// ============================================================================
// microfmt Formatter Specialization
// ============================================================================

/**
 * @brief Formatter rendering a @ref foreign_string_view.
 *
 * Emits `(null)` for the null view and `<invalid-foreign@0x..>` /
 * `<fault@0x..>` markers for read failures.
 */
template <> struct formatter<foreign_string_view> {
  /**
   * @brief No-op parse; foreign strings accept no format specifier.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Renders the foreign string, chunk-by-chunk.
   * @param view The foreign string view to format.
   * @param out Destination sink.
   */
  void format(const foreign_string_view &view, const sink &out) const noexcept {
    if (view.address() == 0) {
      out.write("(null)");
      return;
    }

    if (!view.space() || view.scratch().empty()) {
      microfmt::format_to(out, "<invalid-foreign@{:#x}>", view.address());
      return;
    }

    uintptr_t cur = view.address();
    size_t total = 0;

    while (total < view.max_limit()) {
      auto chunk = view.space().read_string_chunk(cur, view.scratch());
      if (!chunk) {
        if (total == 0) {
          microfmt::format_to(out, "<fault@{:#x}>", view.address());
        } else {
          out.write("<fault>");
        }
        return;
      }

      const size_t chunk_len = chunk->length;
      if (chunk_len > 0) {
        size_t limit_left = view.max_limit() - total;
        size_t to_write = (chunk_len > limit_left) ? limit_left : chunk_len;
        const auto scratch = view.scratch();
        out.write(microfmt::string_view(scratch.data(), to_write));
        total += to_write;
        cur += to_write;
      }

      if (chunk->null_terminated || total >= view.max_limit()) {
        break;
      }
    }

    if (total >= view.max_limit()) {
      out.write("...");
    }
  }
};

} // namespace microfmt