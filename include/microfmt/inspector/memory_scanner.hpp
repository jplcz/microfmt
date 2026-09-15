// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

/** @file memory_scanner.hpp
 * @brief Architecture-generic scanner that processes raw addresses from
 * abstract sources or register contexts using AbiTraits, classifies them, and
 * suppresses hex dumps for sensitive or executable pointer categories. */

#include "address_space.hpp"
#include "dwarf_abi.hpp"
#include "memory_classifier.hpp"
#include "microfmt.hpp"
#include "register_context.hpp"
#include <cstdint>

namespace microfmt {

/**
 * @brief Type-erased abstract source iterator providing addresses to scan.
 */
class address_source_ref {
public:
  struct vtable {
    bool (*next)(const void *ctx, uintptr_t &out_addr) noexcept;
  };

  constexpr address_source_ref() noexcept = default;

  template <typename Context>
  constexpr address_source_ref(const Context &ctx,
                               bool (*next_fn)(const void *,
                                               uintptr_t &) noexcept) noexcept
      : ctx_(&ctx), translate_fn_(next_fn) {}

  [[nodiscard]] bool next(uintptr_t &out_addr) const noexcept {
    if (!translate_fn_ || !ctx_)
      return false;
    return translate_fn_(ctx_, out_addr);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return translate_fn_ != nullptr;
  }

private:
  const void *ctx_{nullptr};
  bool (*translate_fn_)(const void *ctx,
                        uintptr_t &out_addr) noexcept {nullptr};
};

/**
 * @brief Utility scanner parameterized by AbiTraits for inspecting,
 * classifying, and conditional hex-dumping of memory addresses.
 * @tparam AbiTraits Architecture-specific ABI traits.
 */
template <typename AbiTraits> class memory_scanner {
public:
  /**
   * @brief Scans a sequence of raw addresses via span, classifies them, and
   * formats a report.
   */
  static void scan_and_dump(address_space_ref space,
                            memory_classifier_ref classifier,
                            register_context_ref reg_ctx,
                            span<const uintptr_t> raw_addresses,
                            const sink &out, size_t dump_bytes = 8) noexcept {
    size_t index = 0;

    // If a register context is provided, scan all GPR registers from
    // AbiTraits::register_traits
    if (reg_ctx) {
      for (const auto &reg_desc : AbiTraits::register_traits::gpr_registers) {
        typename AbiTraits::register_type reg_val = 0;
        if (reg_ctx.read_raw(reg_desc.index, &reg_val, sizeof(reg_val)) &&
            reg_val != 0) {
          microfmt::format_to(out, MICROFMT_STRING("[{}] -> "), reg_desc.name);
          process_address(space, classifier, index++,
                          static_cast<uintptr_t>(reg_val), out, dump_bytes);
        }
      }
    }

    // Scan explicit raw address span
    for (uintptr_t addr : raw_addresses) {
      process_address(space, classifier, index++, addr, out, dump_bytes);
    }
  }

  /**
   * @brief Scans addresses dynamically pulled from a type-erased abstract
   * source.
   */
  static void scan_and_dump(address_space_ref space,
                            memory_classifier_ref classifier,
                            address_source_ref source, const sink &out,
                            size_t dump_bytes = 8) noexcept {
    if (!source)
      return;

    size_t index = 0;
    uintptr_t addr = 0;
    while (source.next(addr)) {
      process_address(space, classifier, index++, addr, out, dump_bytes);
    }
  }

private:
  static void process_address(address_space_ref space,
                              memory_classifier_ref classifier, size_t index,
                              uintptr_t addr, const sink &out,
                              size_t dump_bytes) noexcept {
    memory_region_info info{};
    bool classified = classifier && classifier.classify_address(addr, info);

    microfmt::format_to(out, MICROFMT_STRING("  #{:<2} addr={:#018x} | type="),
                        index, addr);

    if (classified) {
      print_region_type(out, info.type);
      microfmt::format_to(out, MICROFMT_STRING(" [{:#x} - {:#x}]"),
                          info.start_address, info.end_address);
    } else {
      out.write("unknown/unmapped");
    }

    // Determine whether dereferencing (memory reading/dumping) is permitted for
    // this type
    bool allow_deref = true;
    if (classified) {
      switch (info.type) {
      case memory_region_type::kernel_code:
      case memory_region_type::direct_map:
      case memory_region_type::user_code:
      case memory_region_type::device_mmio:
      case memory_region_type::guard_page:
      case memory_region_type::ns_map:
      case memory_region_type::ns_device_mmio:
        allow_deref = false; // Do not dereference code, direct maps, MMIO,
                             // guard pages, or NS maps
        break;
      default:
        break;
      }
    }

    if (space && dump_bytes > 0 && allow_deref) {
      size_t bytes_to_read = (dump_bytes > 16) ? 16 : dump_bytes;
      uint8_t buffer[16]{};
      if (space.read_bytes(addr, buffer, bytes_to_read)) {
        out.write(" | hex: ");
        for (size_t i = 0; i < bytes_to_read; ++i) {
          microfmt::format_to(out, MICROFMT_STRING("{:02x} "), buffer[i]);
        }
      }
    }
    out.write("\n");
  }

  static void print_region_type(const sink &out,
                                memory_region_type type) noexcept {
    switch (type) {
    case memory_region_type::kernel_code:
      out.write("kernel_code");
      break;
    case memory_region_type::kernel_data:
      out.write("kernel_data");
      break;
    case memory_region_type::kernel_stack:
      out.write("kernel_stack");
      break;
    case memory_region_type::process_stack:
      out.write("process_stack");
      break;
    case memory_region_type::direct_map:
      out.write("direct_map");
      break;
    case memory_region_type::struct_pages:
      out.write("struct_pages");
      break;
    case memory_region_type::kernel_heap:
      out.write("kernel_heap");
      break;
    case memory_region_type::user_code:
      out.write("user_code");
      break;
    case memory_region_type::user_data:
      out.write("user_data");
      break;
    case memory_region_type::device_mmio:
      out.write("device_mmio");
      break;
    case memory_region_type::guard_page:
      out.write("guard_page");
      break;
    case memory_region_type::ns_map:
      out.write("ns_map");
      break;
    case memory_region_type::ns_device_mmio:
      out.write("ns_device_mmio");
      break;
    default:
      out.write("unknown");
      break;
    }
  }
};

} // namespace microfmt