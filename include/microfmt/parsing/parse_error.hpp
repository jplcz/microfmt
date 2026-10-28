// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file parse_error.hpp
 * @brief `parse_error` (a failure that borrows the source) and `stored_error` (the same failure as plain data).
 *
 * Why: a `parse_error` carries tokens whose text points into the parsed source. That is fine while the
 * source is alive, but a streaming parser discards the source as it goes. `stored_error` keeps only the static
 * message strings and the positions, so it can be kept, copied and printed long after the text is gone.
 * See `docs/token-stream.md` ("Streaming input").
 */

#include "lexer.hpp"

namespace microfmt {

/** Why a parse failed. `message` must have static storage (use a string literal). */
struct RELOCO_POINTER parse_error {
  microfmt::string_view message{};
  token at{};                   ///< Offending token; `at.line` / `at.column` locate it, `at.text` borrows the source.
  microfmt::string_view note{}; ///< Optional secondary message (static storage), empty for none.
  token note_at{};              ///< Where `note` points (for example the opening bracket); used when `note` is set.
};

/**
 * @brief Copy of `error` with a note attached, e.g. `with_note(err, "block opened here", open_brace)`.
 *
 * Why: some mistakes are detected far from their cause (a missing `}` is found at the end of input);
 * the note points at the cause. `note` must have static storage; one note per error.
 */
[[nodiscard]] constexpr parse_error with_note(parse_error error, microfmt::string_view note,
                                              const token &where) noexcept {
  error.note = note;
  error.note_at = where;
  return error;
}

/** Where something is in the input: absolute byte offset, 1-based line and column, and the length of the token. */
struct RELOCO_COPYABLE source_site {
  size_t offset{0};
  uint32_t line{1};
  uint32_t column{1};
  uint32_t length{0};
};

/**
 * @brief A `parse_error` without borrowed text: static message strings plus positions. Plain data.
 *
 * Why: it survives any discard of streaming input. Messages are string literals by the `parse_error` rule, so
 * nothing is copied; the offending text itself is not kept, only where it was and how long it was.
 */
struct RELOCO_POINTER stored_error {
  microfmt::string_view message{};
  source_site at{};
  microfmt::string_view note{};
  source_site note_at{};
};

/** The position of `t` as plain data. */
[[nodiscard]] constexpr source_site site_of(const token &t) noexcept {
  return source_site{t.offset, t.line, t.column, static_cast<uint32_t>(t.text.size() & 0xFFFFFFFFu)};
}

/** Detaches `error` from the source. Cannot fail. */
[[nodiscard]] constexpr stored_error store(const parse_error &error) noexcept {
  stored_error result;
  result.message = error.message;
  result.at = site_of(error.at);
  result.note = error.note;
  if (!error.note.empty()) {
    result.note_at = site_of(error.note_at);
  }
  return result;
}

} // namespace microfmt
