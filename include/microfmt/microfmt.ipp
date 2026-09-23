// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file microfmt.ipp
 * @brief Out-of-line bodies for microfmt.hpp's MICROFMT_SHARED-eligible
 * ("heavy", non-templated, or explicitly-instantiated-template) entities.
 *
 * Not meant to be included directly: microfmt.hpp includes this file itself
 * exactly when the current translation unit should see these definitions --
 * i.e. in the default header-only build, or in the single
 * MICROFMT_SHARED_BUILD translation unit that builds the actual shared
 * library (see MICROFMT_SHARED / MICROFMT_SHARED_BUILD in
 * detail/compat.hpp and microfmt_config.hpp). An ordinary MICROFMT_SHARED
 * consumer never includes this file; it only sees the declarations in
 * microfmt.hpp and links against the library built from this file. */

// Included from inside `namespace microfmt { ... }` in microfmt.hpp -- do
// not wrap this file's contents in its own `namespace microfmt` block.

namespace detail {

MICROFMT_API_CONSTEXPR void int_formatter_specs::parse(format_parse_context &ctx) noexcept {
  microfmt::string_view spec = ctx.spec();
  if (spec.empty())
    return;

  size_t i = 0;

  // Parse '#' (alternate form)
  if (i < spec.size() && spec[i] == '#') {
    flags.alt_form = 1;
    ++i;
  }

  // Parse '0' (zero padding flag)
  if (i < spec.size() && spec[i] == '0') {
    flags.zero_pad = 1;
    ++i;
  }

  // Parse width
  while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') {
    width = static_cast<uint8_t>(width * 10 + (spec[i] - '0'));
    ++i;
  }

  // Parse type specifier
  if (i < spec.size()) {
    if (spec[i] == 'x') {
      flags.is_hex = 1;
      flags.uppercase = 0;
    } else if (spec[i] == 'X') {
      flags.is_hex = 1;
      flags.uppercase = 1;
    }
  }
}

} // namespace detail

MICROFMT_API void vformat_to(const sink &out, const microfmt::string_view fmt, const span<const void *const> arg_ptrs,
                             const span<const format_fn_t> arg_fns) noexcept {
  size_t arg_idx = 0;
  size_t i = 0;

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  while (i < fmt.size()) {
    const char *unsafe_fmt = fmt.unsafe_data();
    // UNSAFE: Bounds check in while loop
    const char c = unsafe_fmt[i];

    if (c == '{') {
      // UNSAFE: Bounds check inside this if
      if (i + 1 < fmt.size() && unsafe_fmt[i + 1] == '{') {
        out.put('{');
        i += 2;
        continue;
      }

      size_t close_pos = i + 1;

      // UNSAFE: Bounds check inside while loop
      while (close_pos < fmt.size() && unsafe_fmt[close_pos] != '}') {
        ++close_pos;
      }

      // UNSAFE: Bounds check inside this if
      if (close_pos < fmt.size() && unsafe_fmt[close_pos] == '}') {
        const microfmt::string_view replacement = fmt.substr(i + 1, close_pos - (i + 1));
        const size_t colon_pos = replacement.find(':');
        const microfmt::string_view index =
            colon_pos == microfmt::string_view::npos ? replacement : replacement.substr(0, colon_pos);
        const microfmt::string_view spec =
            colon_pos == microfmt::string_view::npos ? microfmt::string_view{} : replacement.substr(colon_pos + 1);

        size_t selected_arg = arg_idx;
        if (!detail::parse_positional_index(index, selected_arg)) {
          ++arg_idx;
        }

        if (selected_arg < arg_ptrs.size() && selected_arg < arg_fns.size()) {
          // UNSAFE: Explicit validation in if above
          const void *ptr = arg_ptrs.unsafe_at(selected_arg);
          // UNSAFE: Explicit validation in if above
          const format_fn_t fn = arg_fns.unsafe_at(selected_arg);
          if (fn && ptr) {
            fn(ptr, spec, out);
          }
        } else {
          out.write("{MISSING}");
        }
        i = close_pos + 1;
        continue;
      }
      // UNSAFE: Bounds check inside this if
    } else if (c == '}' && i + 1 < fmt.size() && unsafe_fmt[i + 1] == '}') {
      out.put('}');
      i += 2;
      continue;
    }

    out.put(c);
    ++i;
  }
  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

#if defined(MICROFMT_SHARED_BUILD)
// Out-of-line definitions of the two non-template overloads declared under
// MICROFMT_SHARED in microfmt.hpp; each simply forwards to the generic
// template (disambiguated via explicit template arguments, since plain
// `format_int_impl(val, out)` from inside these bodies would resolve back
// to itself instead of the template). Compiled exactly once here, into the
// MICROFMT_SHARED_BUILD library.
namespace detail {
void int_formatter_specs::format_int_impl(int64_t val, const sink &out) const noexcept {
  format_int_impl<int64_t>(val, out);
}

void int_formatter_specs::format_int_impl(uint64_t val, const sink &out) const noexcept {
  format_int_impl<uint64_t>(val, out);
}
} // namespace detail
#endif
