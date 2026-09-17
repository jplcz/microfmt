// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include "dwarf_abi.hpp"

namespace microfmt::gdb {

/**
 * @brief Zero-allocation printer for generating GDB target XML descriptions.
 */
class register_xml_printer {
public:
  /**
   * @brief Formats a single register as a GDB XML `<reg>` node.
   *
   * @param out The sink to write the XML string to.
   * @param reg The register mapping to format.
   */
  static void format_register(sink out, const register_mapping &reg) noexcept {
    // Output format: <reg name="rax" bitsize="64" regnum="0" type="int64"/>
    out.write("<reg name=\"");
    out.write(reg.name);
    out.write("\" bitsize=\"");
    format_to(out, MICROFMT_STRING("{}"), reg.bit_size);
    out.write("\" regnum=\"");
    format_to(out, MICROFMT_STRING("{}"), reg.gdb_index);

    if (!reg.gdb_type.empty()) {
      out.write("\" type=\"");
      out.write(reg.gdb_type);
    }
    out.write("\"/>");
  }

  /**
   * @brief Generates a complete GDB target.xml document for a specific architecture.
   *
   * @tparam AbiTraits The target ABI traits (e.g., aarch64_abi_traits).
   * @param out The sink to write the XML document to.
   * @param gdb_arch_name The architecture string expected by GDB (e.g., "aarch64", "i386:x86-64").
   * @param feature_name The GDB feature namespace (default: "org.gnu.gdb.custom").
   */
  template <typename AbiTraits>
  static void format_target_xml(sink out, string_view gdb_arch_name,
                                string_view feature_name = "org.gnu.gdb.custom") noexcept {
    // XML Header and DOCTYPE
    out.write("<?xml version=\"1.0\"?>\n");
    out.write("<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n");
    out.write("<target>\n");

    // Architecture declaration
    out.write("  <architecture>");
    out.write(gdb_arch_name);
    out.write("</architecture>\n");

    // Feature group block
    out.write("  <feature name=\"");
    out.write(feature_name);
    out.write("\">\n");

    // Iterate over the core, architectural extension, and target-specific
    // register layouts.
    for (const auto &reg : AbiTraits::gdb_register_traits::layout()) {
      out.write("    ");
      format_register(out, reg);
      out.write("\n");
    }
    for (const auto &reg : AbiTraits::gdb_register_traits::extended_layout()) {
      out.write("    ");
      format_register(out, reg);
      out.write("\n");
    }
    for (const auto &reg : AbiTraits::gdb_register_traits::non_standard_layout()) {
      out.write("    ");
      format_register(out, reg);
      out.write("\n");
    }

    out.write("  </feature>\n");
    out.write("</target>\n");
  }
};

} // namespace microfmt::gdb