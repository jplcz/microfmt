// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/address_translator.hpp>
#include <microfmt/sinks/stdio.hpp>

#include <cstddef>
#include <cstdint>

struct demo_translator_tag {};

struct demo_mapping {
  uintptr_t virtual_base;
  uintptr_t physical_base;
  size_t size;
  uint8_t space_id;
  bool writable;
  bool executable;
  bool user_accessible;
};

struct demo_translator_context {
  const demo_mapping *mappings;
  size_t mapping_count;
};

template <> struct microfmt::address_translator_traits<demo_translator_tag> {
  using context_type = demo_translator_context;

  static bool translate(microfmt::value_ref<const context_type> context,
                        uintptr_t virtual_address,
                        microfmt::translation_attributes &attributes) noexcept {
    for (size_t i = 0; i < context->mapping_count; ++i) {
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

      const auto &mapping = context->mappings[i];

      RELOCO_END_UNSAFE_BUFFER_USAGE;

      if (virtual_address < mapping.virtual_base)
        continue;
      const uintptr_t offset = virtual_address - mapping.virtual_base;
      if (offset >= mapping.size)
        continue;

      attributes = {.physical_address = mapping.physical_base + offset,
                    .space_id = mapping.space_id,
                    .is_secure = false,
                    .readable = true,
                    .writable = mapping.writable,
                    .executable = mapping.executable,
                    .user_accessible = mapping.user_accessible};
      return true;
    }
    return false;
  }
};

int main() {
  constexpr demo_mapping mappings[]{{.virtual_base = 0x0000'4000,
                                     .physical_base = 0x0010'0000,
                                     .size = 0x2000,
                                     .space_id = 1,
                                     .writable = false,
                                     .executable = true,
                                     .user_accessible = true},
                                    {.virtual_base = 0xffff'0000,
                                     .physical_base = 0x0020'0000,
                                     .size = 0x1000,
                                     .space_id = 2,
                                     .writable = true,
                                     .executable = false,
                                     .user_accessible = false}};
  const demo_translator_context context{mappings, sizeof(mappings) / sizeof(mappings[0])};
  const auto translator = microfmt::address_translator_ref::make<demo_translator_tag>(context);

  constexpr uintptr_t addresses[]{0x0000'4120, 0xffff'0080, 0x0000'9000};
  for (uintptr_t virtual_address : addresses) {
    microfmt::translation_attributes attributes;
    if (!translator.translate(virtual_address, attributes)) {
      microfmt::println("VA {:#010x} -> unmapped", virtual_address);
      continue;
    }

    microfmt::println("VA {:#010x} -> PA {:#010x} space={} {}{}{}{}", virtual_address, attributes.physical_address,
                      attributes.space_id, attributes.readable ? "r" : "-", attributes.writable ? "w" : "-",
                      attributes.executable ? "x" : "-", attributes.user_accessible ? " user" : " kernel");
  }
}
