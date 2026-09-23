// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file semver.hpp @brief Semantic-version parsing and formatting types. */

#include "../microfmt.hpp"
#include <cstdint>
#include <reloco/type_id.hpp>
#include <string_view>

namespace microfmt {

// ============================================================================
// Semantic Version Descriptor
// ============================================================================

struct MICROFMT_API_CLASS semver {
  uint16_t major{0};
  uint16_t minor{0};
  uint16_t patch{0};
  microfmt::string_view prerelease{}; // e.g. "rc.1", "alpha"
  microfmt::string_view build{};      // e.g. "20260913", "armv7"
  bool show_v_prefix{false};     // e.g. "v1.2.3"
};

// ============================================================================
// Factory Helpers
// ============================================================================

// Explicit components
[[nodiscard]] constexpr semver version(uint16_t major, uint16_t minor,
                                       uint16_t patch,
                                       microfmt::string_view prerelease = "",
                                       microfmt::string_view build = "",
                                       bool show_v = false) noexcept {
  return semver{major, minor, patch, prerelease, build, show_v};
}

// Packed 32-bit integer versions (e.g. 0x010203 -> 1.2.3, 8-bit or
// 10/10/12-bit) Standard 8-8-16 packing: (Major << 24) | (Minor << 16) | Patch
[[nodiscard]] constexpr semver from_packed32(uint32_t packed,
                                             bool show_v = false) noexcept {
  return semver{static_cast<uint16_t>((packed >> 24) & 0xFF),
                static_cast<uint16_t>((packed >> 16) & 0xFF),
                static_cast<uint16_t>(packed & 0xFFFF),
                "",
                "",
                show_v};
}

// Compact 8-8-8 packing: (Major << 16) | (Minor << 8) | Patch
[[nodiscard]] constexpr semver from_packed24(uint32_t packed,
                                             bool show_v = false) noexcept {
  return semver{static_cast<uint16_t>((packed >> 16) & 0xFF),
                static_cast<uint16_t>((packed >> 8) & 0xFF),
                static_cast<uint16_t>(packed & 0xFF),
                "",
                "",
                show_v};
}

// ============================================================================
// Formatter Specialization for semver
// ============================================================================

template <> struct formatter<semver> {
  bool force_v_prefix{false};
  bool hide_prerelease{false};
  bool hide_build{false};

  constexpr void parse(format_parse_context &ctx) noexcept {
    for (char c : ctx.spec()) {
      if (c == '#' || c == 'v' || c == 'V') {
        force_v_prefix = true; // {:#} or {:v} forces "v" prefix
      } else if (c == 'c' || c == 'C') {
        // Compact / Core: {:c} hides prerelease and build metadata
        hide_prerelease = true;
        hide_build = true;
      }
    }
  }

  MICROFMT_API void format(const semver &sv, const sink &out) const noexcept;
};

#if MICROFMT_SHARED_PROVIDE_DEFINITIONS
#include "semver.ipp"
#endif

} // namespace microfmt

// See <reloco/type_id.hpp> for the full RELOCO_TYPE_ID_NAME rationale;
// defined here, alongside semver's own definition.
RELOCO_TYPE_ID_NAME(microfmt::semver, "microfmt::semver");