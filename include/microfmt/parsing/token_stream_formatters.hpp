// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file token_stream_formatters.hpp
 * @brief Formatter that renders a `parse_error` with the offending source line and a caret.
 *
 * Why: every parser user otherwise re-implements "show the line and point at
 * the column". Kept apart from `token_stream.hpp` so the parser helpers do not
 * pull in formatter code. See `docs/token-stream.md` for the user guide.
 */

#include "token_stream.hpp"

namespace microfmt {

/** Borrowing view: `error` and `source` must outlive it. Build it with `report()`. */
struct RELOCO_POINTER error_report {
  const parse_error &error;
  microfmt::string_view source;
  size_t base{0}; ///< Absolute offset of `source[0]` (streaming); 0 for a whole text.
};

/** Borrowing view of a `stored_error`. Build it with `report()`. */
struct RELOCO_POINTER stored_error_report {
  const stored_error &error;
  microfmt::string_view source;
  size_t base{0};
};

/**
 * @param error Failure to show.
 * @param source The text that was parsed (or the current window of a streaming parse).
 * @param base Absolute offset of `source[0]`; `ts.source_base()` for a streaming stream.
 */
[[nodiscard]] constexpr error_report report(const parse_error &error RELOCO_LIFETIMEBOUND,
                                            microfmt::string_view source RELOCO_LIFETIMEBOUND,
                                            size_t base = 0) noexcept {
  return error_report{error, source, base};
}

/** Like the overload above for an error that was stored (`error_list`, `store`). */
[[nodiscard]] constexpr stored_error_report report(const stored_error &error RELOCO_LIFETIMEBOUND,
                                                   microfmt::string_view source RELOCO_LIFETIMEBOUND,
                                                   size_t base = 0) noexcept {
  return stored_error_report{error, source, base};
}

namespace detail {

// "<label> (line:col)", then the source line holding the offset and a caret under its column, but only while that
// byte is inside `src` (the window); otherwise just the label line.
inline void write_snippet(const sink &out, microfmt::string_view src, size_t base, microfmt::string_view label,
                          size_t absolute_offset, uint32_t line, uint32_t column) noexcept {
  out.write(label);
  out.write(" (");
  formatter<uint32_t>{}.format(line, out);
  out.put(':');
  formatter<uint32_t>{}.format(column, out);
  out.put(')');
  if (absolute_offset < base) {
    return;
  }
  const size_t relative = absolute_offset - base;
  const size_t offset = relative < src.size() ? relative : src.size();

  size_t line_start = offset;
  while (line_start > 0 && src[line_start - 1] != '\n') {
    --line_start;
  }
  size_t line_end = offset;
  while (line_end < src.size() && src[line_end] != '\n') {
    ++line_end;
  }
  if (line_end > line_start && src[line_end - 1] == '\r') {
    --line_end;
  }

  out.put('\n');
  out.write(src.substr(line_start, line_end - line_start));
  out.put('\n');
  // Tabs are copied so the caret stays aligned in terminals that expand them.
  for (size_t i = line_start; i < offset; ++i) {
    out.put(src[i] == '\t' ? '\t' : ' ');
  }
  out.put('^');
}

} // namespace detail

/**
 * `{}` prints `message (line:column)`, the source line and a caret line:
 *
 *     expected a value (1:13)
 *     {"a": [1, 2,, 3]}
 *                 ^
 *
 * Offsets past the end of `source` are clamped, so it never reads out of bounds. An error with a
 * note (see `with_note`) adds `note: <text> (line:column)`, its source line and a caret.
 */
template <> struct formatter<error_report> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const error_report &rep, const sink &out) const noexcept {
    detail::write_snippet(out, rep.source, rep.base, rep.error.message, rep.error.at.offset, rep.error.at.line,
                          rep.error.at.column);
    if (!rep.error.note.empty()) {
      out.put('\n');
      out.write("note: ");
      detail::write_snippet(out, rep.source, rep.base, rep.error.note, rep.error.note_at.offset,
                            rep.error.note_at.line, rep.error.note_at.column);
    }
  }
};

/** Same output as for `parse_error`; works long after the source is gone (then only `message (line:column)`). */
template <> struct formatter<stored_error_report> {
  constexpr void parse(format_parse_context &) noexcept {}
  void format(const stored_error_report &rep, const sink &out) const noexcept {
    detail::write_snippet(out, rep.source, rep.base, rep.error.message, rep.error.at.offset, rep.error.at.line,
                          rep.error.at.column);
    if (!rep.error.note.empty()) {
      out.put('\n');
      out.write("note: ");
      detail::write_snippet(out, rep.source, rep.base, rep.error.note, rep.error.note_at.offset,
                            rep.error.note_at.line, rep.error.note_at.column);
    }
  }
};

} // namespace microfmt
