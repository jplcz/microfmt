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
#include "register_context.hpp"
#include "symbol_resolver.hpp"
#include "../formatters/hexdump.hpp"
#include "../microfmt.hpp"
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
 * @brief Optional memory-scanner behavior and caller-owned symbol storage.
 *
 * The scanner borrows @ref symbol_scratch for each symbol lookup and never
 * allocates a symbol-name buffer on its stack.
 */
struct memory_scanner_options {
  /// Maximum number of bytes dumped for a readable data address.
  size_t dump_bytes{80};
  /// Resolver used for addresses classified as code or data.
  symbol_resolver_ref symbol_resolver{};
  /// Whether resolved Itanium names are demangled while printing.
  bool demangle_symbols{true};
};

/**
 * @brief Caller-owned reusable storage for memory scanning.
 *
 * Keep this context in static, arena, or heap storage when stack usage is
 * constrained. A context may be reused between scans but must not be shared by
 * concurrent scans.
 */
struct memory_scanner_context {
  memory_scanner_options options{};
  /// Caller-owned scratch storage passed to the symbol resolver.
  span<char> symbol_scratch{};
  memory_region_info region_info{};
  raw_resolved_symbol raw_symbol{};
  resolved_symbol_info resolved_symbol{};
  hexdump_view dump_view{};
  uint8_t dump_line_buffer[16]{};
  address_space_ref reader_space{};
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
                            memory_scanner_context &context,
                            const sink &out) noexcept {
    size_t index = 0;

    // If a register context is provided, scan the architecture's ordered
    // pointer-bearing GPR candidates.
    if (reg_ctx) {
      for (const auto &reg_desc :
           AbiTraits::register_traits::address_registers()) {
        typename AbiTraits::register_type reg_val = 0;
        if (reg_ctx.read_raw(reg_desc.index, &reg_val, sizeof(reg_val)) &&
            reg_val != 0) {
          microfmt::format_to(out, MICROFMT_STRING("[{}] -> "), reg_desc.name);
          process_address(space, classifier, index++,
                          static_cast<uintptr_t>(reg_val), context, out);
        }
      }
    }

    // Scan explicit raw address span
    for (uintptr_t addr : raw_addresses) {
      process_address(space, classifier, index++, addr, context, out);
    }
  }

  /**
   * @brief Scans addresses dynamically pulled from a type-erased abstract
   * source.
   */
  static void scan_and_dump(address_space_ref space,
                            memory_classifier_ref classifier,
                            address_source_ref source,
                            memory_scanner_context &context,
                            const sink &out) noexcept {
    if (!source)
      return;

    size_t index = 0;
    uintptr_t addr = 0;
    while (source.next(addr)) {
      process_address(space, classifier, index++, addr, context, out);
    }
  }

private:
  static void process_address(address_space_ref space,
                              memory_classifier_ref classifier, size_t index,
                              uintptr_t addr, memory_scanner_context &context,
                              const sink &out) noexcept {
    context.region_info = {};
    bool classified =
        classifier &&
        classifier.classify_address(addr, context.region_info);
    const auto &info = context.region_info;

    microfmt::format_to(out, MICROFMT_STRING("  #{:<2} addr={:#018x} | type="),
                        index, addr);

    if (classified) {
      print_region_type(out, info.type);
      microfmt::format_to(out, MICROFMT_STRING(" [{:#x} - {:#x}]"),
                          info.start_address, info.end_address);
    } else {
      out.write("unknown/unmapped");
    }

    if (classified && context.options.symbol_resolver &&
        is_symbolic_region(info.type)) {
      context.resolved_symbol = {};
      if (context.options.symbol_resolver.resolve(
              addr, context.symbol_scratch, context.raw_symbol,
              context.resolved_symbol) &&
          context.resolved_symbol.has_symbol()) {
        const auto &symbol = context.resolved_symbol;
        out.write(" | symbol=");
        if (context.options.demangle_symbols) {
          microfmt::format_to(out, MICROFMT_STRING("{}"),
                              as_demangled(symbol.symbol_name));
        } else {
          out.write(symbol.symbol_name);
        }
        if (symbol.offset_from_symbol != 0) {
          microfmt::format_to(out, MICROFMT_STRING("+{:#x}"),
                              symbol.offset_from_symbol);
        }
      }
    }

    // Determine whether dereferencing (memory reading/dumping) is permitted for
    // this type
    bool allow_deref = classified && info.readable;
    if (allow_deref) {
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

    out.write("\n");

    if (!space || context.options.dump_bytes == 0 || !allow_deref)
      return;

    size_t bytes_to_dump =
        (context.options.dump_bytes > 80) ? 80
                                          : context.options.dump_bytes;
    if (classified && info.end_address > addr) {
      const size_t bytes_in_region =
          static_cast<size_t>(info.end_address - addr);
      if (bytes_to_dump > bytes_in_region)
        bytes_to_dump = bytes_in_region;
    }

    auto reader = [](void *context, uintptr_t source_address, uint8_t *buffer,
                     size_t size) noexcept -> size_t {
      if (!context)
        return 0;
      const auto &target_space =
          *static_cast<const address_space_ref *>(context);
      return target_space.read_bytes(source_address, buffer, size) ? size : 0;
    };
    context.reader_space = space;
    context.dump_view =
        hexdump_checked(addr, bytes_to_dump, reader, &context.reader_space, 16,
                        true);
    format_hexdump(context.dump_view,
                   {context.dump_line_buffer,
                    sizeof(context.dump_line_buffer)},
                   out);
  }

  [[nodiscard]] static constexpr bool
  is_symbolic_region(memory_region_type type) noexcept {
    switch (type) {
    case memory_region_type::kernel_code:
    case memory_region_type::kernel_data:
    case memory_region_type::user_code:
    case memory_region_type::user_data:
      return true;
    default:
      return false;
    }
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