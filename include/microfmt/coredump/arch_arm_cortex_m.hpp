// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_arm_cortex_m.hpp
 * @brief Architecture policy for 32-bit ARM Cortex-M cores, for `elf_core_writer`.
 *
 * Why: Cortex-M microcontrollers are the main place where a streaming,
 * allocation-free core writer is needed. They are 32-bit, little-endian and use
 * the ARM ELF machine number. The register block in `NT_PRSTATUS` follows the
 * Linux ARM layout, which is what `gdb` expects for `EM_ARM` cores: r0-r15, the
 * program status register and the "original r0" slot (always zero here).
 *
 * `arch_arm_cortex_m<true>` (alias `arch_arm_cortex_m_vfp`) additionally writes
 * one `NT_ARM_VFP` register set per thread (the 32 double-precision registers plus the floating-point status
 * register; VFP: Vector Floating Point unit) so floating-point variables show correctly. Registers that cannot
 * be read are stored as zero.
 *
 * The program status register slot is filled from the Cortex-M `xPSR`. The `NT_ARM_VFP` register set is
 * fetched as a blob (see `core_regset`); the shared implementation lives in `arch_arm32.hpp`.
 */

#include "arch_arm32.hpp"

namespace microfmt::coredump {

// 256-byte page granule: no MMU, so holes are found precisely.
template <bool WithVfp = false>
using arch_arm_cortex_m_t = detail::arm32_policy<256, static_cast<std::uint32_t>(dwarf::arm32::xpsr), WithVfp>;

using arch_arm_cortex_m = arch_arm_cortex_m_t<false>;
using arch_arm_cortex_m_vfp = arch_arm_cortex_m_t<true>;

} // namespace microfmt::coredump
