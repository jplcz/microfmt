// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file arch_arm_cortex_a.hpp
 * @brief Architecture policy for 32-bit ARM Cortex-A cores (ARM or Thumb state), for `elf_core_writer`.
 *
 * Why: application-profile ARM cores run Linux-style systems with an MMU, so they use 4 KiB pages and report their
 * program status in the `cpsr` (current program status register) instead of the Cortex-M `xPSR`. The register
 * layout is otherwise the Linux ARM one that `gdb` expects for `EM_ARM` cores. `arch_arm_cortex_a_vfp` adds the
 * `NT_ARM_VFP` register set so floating-point and NEON (Advanced SIMD) values show up. 64-bit cores use
 * `arch_aarch64`.
 */

#include "arch_arm32.hpp"

namespace microfmt::coredump {

/** @brief Cortex-A policy; see `arm32_policy` for the optional `NT_ARM_TLS` and hardware debug sets. */
template <bool WithVfp = false, bool WithTls = false, std::uint32_t Breakpoints = 0, std::uint32_t Watchpoints = 0>
using arch_arm_cortex_a_t = detail::arm32_policy<4096, static_cast<std::uint32_t>(dwarf::arm32::cpsr), WithVfp, WithTls,
                                                 Breakpoints, Watchpoints>;

using arch_arm_cortex_a = arch_arm_cortex_a_t<false>;
using arch_arm_cortex_a_vfp = arch_arm_cortex_a_t<true>;

} // namespace microfmt::coredump
