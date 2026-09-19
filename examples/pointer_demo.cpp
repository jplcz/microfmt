// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdint>
#include <cstdio>
#include <microfmt/formatters/pointer.hpp>
#include <microfmt/microfmt.hpp>
#include <string_view>

namespace {

void stdout_write(void * /*context*/, microfmt::string_view text) noexcept {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::fwrite(text.data(), 1, text.size(), stdout);

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}

} // namespace

int main() {
  const microfmt::sink output{nullptr, stdout_write};
  const uintptr_t peripheral = UINT64_C(0x40021018);
  const std::array<uint16_t, 4> registers{{0x00A1, 0x000F, 0x1234, 0xBEEF}};

  microfmt::format_to(output, "native address: {}\n", microfmt::raw_ptr(peripheral));
  microfmt::format_to(output, "32-bit target:  {}\n", microfmt::raw_ptr32(peripheral));
  microfmt::format_to(output, "64-bit target:  {:64P}\n", microfmt::raw_ptr(peripheral));
  microfmt::format_to(output, "null pointer:   {:z}\n", microfmt::raw_ptr(nullptr));
  microfmt::format_to(output, "registers:      {:c04X}\n", microfmt::raw_range(registers.data(), registers.size()));

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  microfmt::format_to(output, "register slice: {:n02x}\n", microfmt::raw_range(registers.data() + 1, size_t{2}));

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
