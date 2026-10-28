// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdio>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/number_parse_float.hpp>

int main() {
  // Strict, Rust-like: the whole text must be a number; errors say why and where.
  auto port = microfmt::parse<std::uint16_t>("8080");
  auto big = microfmt::parse<std::uint8_t>("300");
  std::printf("port ok=%d value=%u\n", port.has_value(), port.has_value() ? port.value() : 0U);
  std::printf("300 as u8: overflow=%d at offset %zu\n", !big.has_value() && big.error().code == microfmt::number_errc::overflow,
              big.has_value() ? 0U : big.error().offset);

  // Options: detect 0x/0b/0o prefixes and allow '_' separators (what the TOML lexer produces).
  auto mask = microfmt::parse<std::uint32_t>("0xdead_beef", microfmt::integer_options{.radix = 0, .separator = '_'});
  std::printf("mask=%08x\n", mask.has_value() ? mask.value() : 0U);

  // Prefix parse (from_chars-like): stops at the first byte that is not part of the number.
  auto pre = microfmt::parse_prefix<int>("42px");
  std::printf("prefix value=%d consumed=%zu\n", pre.value().value, pre.value().consumed);

  // Floating point is opt-in (separate header).
  auto pi = microfmt::parse<double>("3.14159");
  std::printf("pi=%.5f\n", pi.has_value() ? pi.value() : 0.0);
  return 0;
}
