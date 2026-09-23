// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

/** @file semver.ipp @brief Out-of-line body for formatter<semver>::format
 * (see semver.hpp). Included from semver.hpp itself, guarded on
 * MICROFMT_SHARED_PROVIDE_DEFINITIONS (see microfmt/detail/compat.hpp).
 * Never included directly. */

MICROFMT_API void formatter<semver>::format(const semver &sv, const sink &out) const noexcept {
  if (sv.show_v_prefix || force_v_prefix) {
    out.put('v');
  }

  // Major.Minor.Patch
  detail::format_unsigned<detail::radix::decimal>(out, sv.major, false, 0);
  out.put('.');
  detail::format_unsigned<detail::radix::decimal>(out, sv.minor, false, 0);
  out.put('.');
  detail::format_unsigned<detail::radix::decimal>(out, sv.patch, false, 0);

  // Prerelease: -rc.1
  if (!hide_prerelease && !sv.prerelease.empty()) {
    out.put('-');
    out.write(sv.prerelease);
  }

  // Build metadata: +build.42
  if (!hide_build && !sv.build.empty()) {
    out.put('+');
    out.write(sv.build);
  }
}
