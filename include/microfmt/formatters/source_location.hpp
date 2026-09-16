// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file source_location.hpp @brief Standard and Boost source-location formatters. */

#include "../microfmt.hpp"
#include <cstdint>
#include <string_view>

namespace microfmt {

#if MICROFMT_HAS_SOURCE_LOCATION
template <typename Location> struct basic_source_loc_view {
  Location loc;
  bool show_function{true};
  bool file_basename_only{true};
};

#if MICROFMT_HAS_STD_SOURCE_LOCATION
using std_source_loc_view = basic_source_loc_view<std::source_location>;

[[nodiscard]] constexpr std_source_loc_view
source_loc(std::source_location loc = std::source_location::current(),
           bool show_fn = true, bool basename_only = true) noexcept {
  return std_source_loc_view{loc, show_fn, basename_only};
}
#endif

#if MICROFMT_HAS_BOOST_SOURCE_LOCATION
using boost_source_loc_view = basic_source_loc_view<boost::source_location>;

[[nodiscard]] constexpr boost_source_loc_view
source_loc(boost::source_location loc, bool show_fn = true,
           bool basename_only = true) noexcept {
  return boost_source_loc_view{loc, show_fn, basename_only};
}
#endif

#if MICROFMT_HAS_STD_SOURCE_LOCATION
using source_loc_view = std_source_loc_view;
#elif MICROFMT_HAS_BOOST_SOURCE_LOCATION
using source_loc_view = boost_source_loc_view;
#endif

template <typename Location> struct formatter<basic_source_loc_view<Location>> {
  bool parse_short{false};
  bool parse_fn{true};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == 's' || c == 'S')
        parse_short = true; // {:s} -> file:line only
      if (c == 'f' || c == 'F')
        parse_fn = true;
    }
  }

  void format(const basic_source_loc_view<Location> &sv,
              const sink &out) const noexcept {
    microfmt::string_view file = sv.loc.file_name();

    if (sv.file_basename_only) {
      // Extract filename after last '/' or '\'
      size_t last_slash = file.find_last_of("/\\");
      if (last_slash != microfmt::string_view::npos) {
        file = file.substr(last_slash + 1);
      }
    }

    out.write(file);
    out.put(':');
    detail::format_unsigned(out, sv.loc.line(), 10, false, 0);

    const bool emit_fn = (sv.show_function && parse_fn && !parse_short);
    if (emit_fn) {
      out.write(" in ");
      out.write(sv.loc.function_name());
      out.write("()");
    }
  }
};

#if MICROFMT_HAS_STD_SOURCE_LOCATION
template <> struct formatter<std::source_location> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const std::source_location &loc, const sink &out) const noexcept {
    formatter<std_source_loc_view> f;
    f.format(source_loc(loc), out);
  }
};
#endif

#if MICROFMT_HAS_BOOST_SOURCE_LOCATION
template <> struct formatter<boost::source_location> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const boost::source_location &loc,
              const sink &out) const noexcept {
    formatter<boost_source_loc_view> f;
    f.format(source_loc(loc), out);
  }
};
#endif

#endif // MICROFMT_HAS_SOURCE_LOCATION

} // namespace microfmt
