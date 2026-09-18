// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"

namespace microfmt::inspector {

enum class token_type {
  end,
  identifier, // e.g. "x0", "a", "print_hex"
  number,     // e.g. "42", "0x10"
  assign,     // "="
  plus,       // "+"
  minus,      // "-"
  semicolon   // ";"
};

struct token {
  token_type type;
  string_view text;
  uint64_t num_val;
};

class MICROFMT_POINTER vm_lexer {
public:
  constexpr explicit vm_lexer(string_view source MICROFMT_LIFETIMEBOUND MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : sv_(source) {
    advance();
  }

  [[nodiscard]] constexpr const token &current() const & noexcept MICROFMT_LIFETIMEBOUND { return current_; }

  constexpr void advance() noexcept {
    // Skip whitespace and newlines
    while (!sv_.empty() && (sv_.front() == ' ' || sv_.front() == '\t' || sv_.front() == '\r' || sv_.front() == '\n')) {
      sv_.remove_prefix(1);
    }

    if (sv_.empty()) {
      current_ = {token_type::end, {}, 0};
      return;
    }

    char c = sv_.front();

    // Single-character punctuation
    if (c == '=') {
      sv_.remove_prefix(1);
      current_ = {token_type::assign, "=", 0};
      return;
    }
    if (c == '+') {
      sv_.remove_prefix(1);
      current_ = {token_type::plus, "+", 0};
      return;
    }
    if (c == '-') {
      sv_.remove_prefix(1);
      current_ = {token_type::minus, "-", 0};
      return;
    }
    if (c == ';') {
      sv_.remove_prefix(1);
      current_ = {token_type::semicolon, ";", 0};
      return;
    }

    // Number literals (decimal or hex)
    if (c >= '0' && c <= '9') {
      uint64_t val = 0;
      bool is_hex = false;
      const char *start = sv_.data();

      if (c == '0' && sv_.size() > 1 && (sv_[1] == 'x' || sv_[1] == 'X')) {
        is_hex = true;
        sv_.remove_prefix(2);
      }

      while (!sv_.empty()) {
        char nc = sv_.front();
        uint64_t digit = 16;
        if (nc >= '0' && nc <= '9')
          digit = (uint64_t)(nc - '0');
        else if (is_hex && nc >= 'a' && nc <= 'f')
          digit = (uint64_t)(nc - 'a' + 10);
        else if (is_hex && nc >= 'A' && nc <= 'F')
          digit = (uint64_t)(nc - 'A' + 10);
        else
          break;

        val = val * (is_hex ? 16 : 10) + digit;
        sv_.remove_prefix(1);
      }
      current_ = {token_type::number, {start, static_cast<size_t>(sv_.data() - start)}, val};
      return;
    }

    // Identifiers and Keywords
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      const char *start = sv_.data();
      size_t len = 0;
      while (!sv_.empty() &&
             ((sv_.front() >= 'a' && sv_.front() <= 'z') || (sv_.front() >= 'A' && sv_.front() <= 'Z') ||
              (sv_.front() >= '0' && sv_.front() <= '9') || sv_.front() == '_')) {
        len++;
        sv_.remove_prefix(1);
      }
      current_ = {token_type::identifier, {start, len}, 0};
      return;
    }

    // Unrecognized character
    sv_.remove_prefix(1);
    current_ = {token_type::end, {}, 0};
  }

private:
  string_view sv_;
  token current_{token_type::end, {}, 0};
};

} // namespace microfmt::inspector