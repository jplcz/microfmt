// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file demangle.hpp @brief Zero-allocation Itanium (GCC/Clang) name
 * demangler. */

#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace microfmt {

/**
 * @brief Zero-allocation Itanium (GCC/Clang) name demangler.
 */
class itanium_demangler {
public:
  /**
   * @brief Constructs a demangler over an Itanium-mangled symbol.
   * @param mangled Source symbol (must start with `_Z`).
   */
  constexpr explicit itanium_demangler(std::string_view mangled) noexcept
      : src_(mangled), pos_(0) {}

  /**
   * @brief Demangles the symbol into the sink.
   * @param out Destination sink receiving the demangled form.
   * @return `false` when the input is not an Itanium-mangled name.
   */
  bool demangle_to(const sink &out) noexcept {
    if (!detail::starts_with(src_, "_Z")) {
      return false;
    }

    pos_ = 2; // Skip "_Z"

    // Virtual Tables & RTTI prefixes
    if (match_prefix("TV")) {
      out.write("vtable for ");
      return parse_type(out);
    }
    if (match_prefix("TT")) {
      out.write("VTT for ");
      return parse_type(out);
    }
    if (match_prefix("TI")) {
      out.write("typeinfo for ");
      return parse_type(out);
    }
    if (match_prefix("TS")) {
      out.write("typeinfo name for ");
      return parse_type(out);
    }

    // Standard symbols (functions / methods / encodings)
    return parse_encoding(out);
  }

  /**
   * @brief Demangles a symbol, falling back to the raw input on failure.
   * @param sym Mangled symbol to demangle.
   * @param out Destination sink.
   */
  static void format_symbol(std::string_view sym, const sink &out) noexcept {
    itanium_demangler d(sym);
    if (!d.demangle_to(out)) {
      out.write(sym); // Fallback to raw symbol
    }
  }

private:
  /// Mangled source.
  std::string_view src_;
  /// Read position within the source.
  size_t pos_{0};
  /// Recursion guard depth.
  static constexpr size_t kMaxRecursionDepth = 16;
  /// Current recursion depth.
  size_t depth_{0};

  /**
   * @brief Reports whether reading is past the end of the source.
   */
  [[nodiscard]] constexpr bool eof() const noexcept {
    return pos_ >= src_.size();
  }
  /**
   * @brief Returns the current character without consuming it.
   */
  [[nodiscard]] constexpr char peek() const noexcept {
    return eof() ? '\0' : src_[pos_];
  }
  /**
   * @brief Consumes and returns the current character.
   */
  constexpr char get() noexcept { return eof() ? '\0' : src_[pos_++]; }

  /**
   * @brief Consumes @p c if it is next.
   */
  constexpr bool match(char c) noexcept {
    if (peek() == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  /**
   * @brief Consumes @p p if it matches at the current position.
   */
  constexpr bool match_prefix(std::string_view p) noexcept {
    if (detail::starts_with(src_.substr(pos_), p)) {
      pos_ += p.size();
      return true;
    }
    return false;
  }

  /**
   * @brief Parses a base-10 number into @p val.
   */
  bool parse_number(size_t &val) noexcept {
    if (eof() || peek() < '0' || peek() > '9')
      return false;
    val = 0;
    while (!eof() && peek() >= '0' && peek() <= '9') {
      val = val * 10 + static_cast<size_t>(get() - '0');
    }
    return true;
  }

  /**
   * @brief Parses `<encoding>`: name plus optional bare-function-type.
   */
  bool parse_encoding(const sink &out) noexcept {
    if (!parse_name(out))
      return false;

    // Trailing function parameter types
    if (!eof() && peek() != 'E') {
      if (peek() == 'v' && (pos_ + 1 == src_.size() || src_[pos_ + 1] == 'E')) {
        get(); // Consume 'v'
        out.write("()");
      } else {
        out.write("(");
        bool first = true;
        while (!eof() && peek() != 'E') {
          if (!first)
            out.write(", ");
          first = false;
          if (!parse_type(out))
            return false;
        }
        out.write(")");
      }
    }
    return true;
  }

  /**
   * @brief Parses `<name>`: nested, unscoped, or substituted.
   */
  bool parse_name(const sink &out) noexcept {
    if (depth_++ > kMaxRecursionDepth)
      return false;

    bool ok = false;
    if (match('N')) {
      ok = parse_nested_name(out);
    } else if (match('S')) {
      ok = parse_substitution(out);
      if (ok && match('I')) {
        ok = parse_template_args(out);
      }
    } else {
      ok = parse_unqualified_name(out);
      if (ok && match('I')) {
        ok = parse_template_args(out);
      }
    }

    --depth_;
    return ok;
  }

  /**
   * @brief Parses `<template-args>` between `<` and `>`.
   */
  bool parse_template_args(const sink &out) noexcept {
    out.write("<");
    bool first = true;
    while (!eof() && peek() != 'E') {
      if (!first)
        out.write(", ");
      first = false;
      if (!parse_type(out))
        return false;
    }
    out.write(">");
    return match('E');
  }

  /**
   * @brief Parses `<nested-name>`, joining components with `::`.
   */
  bool parse_nested_name(const sink &out) noexcept {
    bool first = true;
    while (!eof() && peek() != 'E') {
      // Consume trailing CV qualifiers on member methods
      if (peek() == 'r' || peek() == 'V' || peek() == 'K') {
        get();
        continue;
      }

      if (match('I')) {
        if (!parse_template_args(out))
          return false;
        continue;
      }

      if (!first)
        out.write("::");
      first = false;

      if (match('S')) {
        if (!parse_substitution(out))
          return false;
      } else if (!parse_unqualified_name(out)) {
        return false;
      }
    }
    return match('E');
  }

  /**
   * @brief Parses an unscoped name: length-prefixed identifiers, constructors,
   * destructors, and operator symbols.
   */
  bool parse_unqualified_name(const sink &out) noexcept {
    if (peek() >= '0' && peek() <= '9') {
      size_t len = 0;
      if (!parse_number(len))
        return false;
      if (pos_ + len > src_.size())
        return false;
      out.write(src_.substr(pos_, len));
      pos_ += len;
      return true;
    }

    // Constructors / Destructors
    if (match_prefix("C1") || match_prefix("C2") || match_prefix("C3")) {
      out.write("{constructor}");
      return true;
    }
    if (match_prefix("D1") || match_prefix("D2") || match_prefix("D0")) {
      out.write("~{destructor}");
      return true;
    }

    // Operator symbols
    if (match_prefix("nw")) {
      out.write("operator new");
      return true;
    }
    if (match_prefix("dl")) {
      out.write("operator delete");
      return true;
    }
    if (match_prefix("na")) {
      out.write("operator new[]");
      return true;
    }
    if (match_prefix("da")) {
      out.write("operator delete[]");
      return true;
    }
    if (match_prefix("pl")) {
      out.write("operator+");
      return true;
    }
    if (match_prefix("mi")) {
      out.write("operator-");
      return true;
    }
    if (match_prefix("ml")) {
      out.write("operator*");
      return true;
    }
    if (match_prefix("dv")) {
      out.write("operator/");
      return true;
    }
    if (match_prefix("eq")) {
      out.write("operator==");
      return true;
    }
    if (match_prefix("ne")) {
      out.write("operator!=");
      return true;
    }
    if (match_prefix("ix")) {
      out.write("operator[]");
      return true;
    }
    if (match_prefix("cl")) {
      out.write("operator()");
      return true;
    }

    return false;
  }

  /**
   * @brief Parses a substitution reference (`S...`).
   */
  bool parse_substitution(const sink &out) noexcept {
    // 'St' is the prefix for std:: namespace
    if (match('t')) {
      out.write("std::");
      return parse_unqualified_name(out);
    }
    if (match('a')) {
      out.write("std::allocator");
      return true;
    }
    if (match('b')) {
      out.write("std::basic_string");
      return true;
    }
    if (match('s')) {
      out.write("std::string");
      return true;
    }
    if (match('i')) {
      out.write("std::istream");
      return true;
    }
    if (match('o')) {
      out.write("std::ostream");
      return true;
    }
    if (match('d')) {
      out.write("std::iostream");
      return true;
    }

    // S_ or S<seq>_ (numbered substitutions)
    if (match('_')) {
      out.write("self");
      return true;
    }

    while (!eof() && peek() != '_') {
      get();
    }
    match('_');
    out.write("T");
    return true;
  }

  /**
   * @brief Parses a `<type>`: qualifiers, pointers, builtins, or named types.
   */
  bool parse_type(const sink &out) noexcept {
    if (depth_++ > kMaxRecursionDepth)
      return false;

    // Type qualifiers & pointer/reference modifiers
    if (match('P')) {
      bool ok = parse_type(out);
      out.write("*");
      --depth_;
      return ok;
    }
    if (match('R')) {
      bool ok = parse_type(out);
      out.write("&");
      --depth_;
      return ok;
    }
    if (match('O')) {
      bool ok = parse_type(out);
      out.write("&&");
      --depth_;
      return ok;
    }
    if (match('K')) {
      bool ok = parse_type(out);
      out.write(" const");
      --depth_;
      return ok;
    }
    if (match('V')) {
      bool ok = parse_type(out);
      out.write(" volatile");
      --depth_;
      return ok;
    }

    // Builtin primitives
    switch (peek()) {
    case 'v':
      get();
      out.write("void");
      --depth_;
      return true;
    case 'w':
      get();
      out.write("wchar_t");
      --depth_;
      return true;
    case 'b':
      get();
      out.write("bool");
      --depth_;
      return true;
    case 'c':
      get();
      out.write("char");
      --depth_;
      return true;
    case 'a':
      get();
      out.write("signed char");
      --depth_;
      return true;
    case 'h':
      get();
      out.write("unsigned char");
      --depth_;
      return true;
    case 's':
      get();
      out.write("short");
      --depth_;
      return true;
    case 't':
      get();
      out.write("unsigned short");
      --depth_;
      return true;
    case 'i':
      get();
      out.write("int");
      --depth_;
      return true;
    case 'j':
      get();
      out.write("unsigned int");
      --depth_;
      return true;
    case 'l':
      get();
      out.write("long");
      --depth_;
      return true;
    case 'm':
      get();
      out.write("unsigned long");
      --depth_;
      return true;
    case 'x':
      get();
      out.write("long long");
      --depth_;
      return true;
    case 'y':
      get();
      out.write("unsigned long long");
      --depth_;
      return true;
    case 'f':
      get();
      out.write("float");
      --depth_;
      return true;
    case 'd':
      get();
      out.write("double");
      --depth_;
      return true;
    case 'e':
      get();
      out.write("long double");
      --depth_;
      return true;
    case 'z':
      get();
      out.write("...");
      --depth_;
      return true;
    default:
      break;
    }

    // Compound / nested / substituted type name
    bool ok = parse_name(out);
    --depth_;
    return ok;
  }
};

/**
 * @brief Formattable view over a demangled symbol.
 */
struct demangle_view {
  /// Mangled symbol.
  std::string_view symbol;
};

/**
 * @brief Wraps a symbol for demangled formatting.
 * @param sym Mangled symbol.
 * @return A @ref demangle_view over the symbol.
 */
[[nodiscard]] constexpr auto as_demangled(std::string_view sym) noexcept {
  return demangle_view{sym};
}

/**
 * @brief Formatter for @ref demangle_view.
 */
template <> struct formatter<demangle_view> {
  /**
   * @brief No-op parse.
   */
  constexpr void parse(format_parse_context &) noexcept {}

  /**
   * @brief Demangles and writes the symbol.
   * @param dv The demangle view to format.
   * @param out Destination sink.
   */
  void format(const demangle_view &dv, const sink &out) const noexcept {
    itanium_demangler::format_symbol(dv.symbol, out);
  }
};

} // namespace microfmt