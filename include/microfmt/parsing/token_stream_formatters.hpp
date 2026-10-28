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
};

/** @param error Failure to show. @param source The exact text that was parsed. */
[[nodiscard]] constexpr error_report report(const parse_error &error RELOCO_LIFETIMEBOUND,
                                            microfmt::string_view source RELOCO_LIFETIMEBOUND) noexcept {
  return error_report{error, source};
}

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
    write_snippet(out, rep.source, rep.error.message, rep.error.at);
    if (!rep.error.note.empty()) {
      out.put('\n');
      out.write("note: ");
      write_snippet(out, rep.source, rep.error.note, rep.error.note_at);
    }
  }

private:
  // "<label> (line:col)", the source line holding `at`, and a caret under its column.
  static void write_snippet(const sink &out, microfmt::string_view src, microfmt::string_view label,
                            const token &at) noexcept {
    const size_t offset = at.offset < src.size() ? at.offset : src.size();

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

    out.write(label);
    out.write(" (");
    formatter<uint32_t>{}.format(at.line, out);
    out.put(':');
    formatter<uint32_t>{}.format(at.column, out);
    out.write(")\n");
    out.write(src.substr(line_start, line_end - line_start));
    out.put('\n');
    // Tabs are copied so the caret stays aligned in terminals that expand them.
    for (size_t i = line_start; i < offset; ++i) {
      out.put(src[i] == '\t' ? '\t' : ' ');
    }
    out.put('^');
  }
};

} // namespace microfmt
