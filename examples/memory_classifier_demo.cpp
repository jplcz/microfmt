// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/memory_classifier.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <cstddef>
#include <cstdint>

struct demo_classifier_tag {};

struct demo_classifier_context {
  const microfmt::memory_region_info *regions;
  size_t region_count;
};

template <> struct microfmt::memory_classifier_traits<demo_classifier_tag> {
  using context_type = demo_classifier_context;

  static bool classify_address(const void *opaque_context, uintptr_t virtual_address,
                               microfmt::memory_region_info &info) noexcept {
    if (!opaque_context)
      return false;
    const auto &context = *static_cast<const demo_classifier_context *>(opaque_context);

    MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

    for (size_t i = 0; i < context.region_count; ++i) {
      if (context.regions[i].contains(virtual_address)) {
        info = context.regions[i];
        return true;
      }
    }
    MICROFMT_END_UNSAFE_BUFFER_USAGE;
    return false;
  }
};

constexpr microfmt::string_view region_name(microfmt::memory_region_type type) noexcept {
  using enum microfmt::memory_region_type;
  switch (type) {
  case kernel_code:
    return "kernel code";
  case process_stack:
    return "process stack";
  case device_mmio:
    return "device MMIO";
  default:
    return "unknown";
  }
}

int main() {
  constexpr microfmt::memory_region_info regions[]{{.start_address = 0x8000'0000,
                                                    .end_address = 0x8001'0000,
                                                    .type = microfmt::memory_region_type::kernel_code,
                                                    .space_id = 0,
                                                    .readable = true,
                                                    .writable = false,
                                                    .executable = true},
                                                   {.start_address = 0x7fff'0000,
                                                    .end_address = 0x8000'0000,
                                                    .type = microfmt::memory_region_type::process_stack,
                                                    .space_id = 17,
                                                    .readable = true,
                                                    .writable = true,
                                                    .executable = false},
                                                   {.start_address = 0x4000'0000,
                                                    .end_address = 0x4000'1000,
                                                    .type = microfmt::memory_region_type::device_mmio,
                                                    .space_id = 0,
                                                    .readable = true,
                                                    .writable = true,
                                                    .executable = false}};
  const demo_classifier_context context{regions, sizeof(regions) / sizeof(regions[0])};
  const auto classifier = microfmt::memory_classifier_ref::make<demo_classifier_tag>(context);

  constexpr uintptr_t addresses[]{0x8000'1234, 0x7fff'f000, 0x4000'0040, 0x1234'5678};
  for (uintptr_t address : addresses) {
    microfmt::memory_region_info info;
    if (!classifier.classify_address(address, info)) {
      microfmt::println("{:#010x}: unknown", address);
      continue;
    }

    microfmt::println("{:#010x}: {} [{:#010x}, {:#010x}) space={} {}{}{}", address, region_name(info.type),
                      info.start_address, info.end_address, info.space_id, info.readable ? "r" : "-",
                      info.writable ? "w" : "-", info.executable ? "x" : "-");
  }
}
