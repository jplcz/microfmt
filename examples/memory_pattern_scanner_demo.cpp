// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/memory_pattern_scanner.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

int main() {
  const std::array<std::byte, 12> memory{
      std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0xde},
      std::byte{0xad}, std::byte{0x7f}, std::byte{0xef}, std::byte{0x40},
      std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80}};
  std::array<std::byte, 5> scratch{};

  microfmt::memory_pattern_scanner<microfmt::linear_memory_scanner_tag> scanner{
      microfmt::linear_memory_scanner_context{
          microfmt::address_space_ref{microfmt::local_space_tag{}},
          microfmt::span<std::byte>(scratch.data(), scratch.size())}};

  const std::array<std::byte, 4> exact{
      std::byte{0xde}, std::byte{0xad}, std::byte{0x7f}, std::byte{0xef}};
  const auto exact_result =
      scanner.ref().scan(reinterpret_cast<uintptr_t>(memory.data()),
                         memory.size(),
                         microfmt::span<const std::byte>(exact.data(),
                                                         exact.size()));

  const std::array<std::byte, 4> masked{
      std::byte{0xde}, std::byte{0xad}, std::byte{0x00}, std::byte{0xef}};
  const std::array<std::byte, 4> mask{
      std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xff}};
  const auto masked_result =
      scanner.ref().scan(reinterpret_cast<uintptr_t>(memory.data()),
                         memory.size(),
                         microfmt::span<const std::byte>(masked.data(),
                                                         masked.size()),
                         microfmt::span<const std::byte>(mask.data(),
                                                         mask.size()));

  if (exact_result && exact_result->found) {
    microfmt::println("exact match at offset {}",
                      exact_result->address -
                          reinterpret_cast<uintptr_t>(memory.data()));
  }
  if (masked_result && masked_result->found) {
    microfmt::println("masked match at offset {}",
                      masked_result->address -
                          reinterpret_cast<uintptr_t>(memory.data()));
  }
}
