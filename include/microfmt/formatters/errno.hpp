// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include <cerrno>

#ifndef MICROFMT_KERNEL
#ifndef MICROFMT_USE_SYSTEM_ERROR
#define MICROFMT_USE_SYSTEM_ERROR 0
#endif
#if MICROFMT_USE_SYSTEM_ERROR
#include <string>
#include <system_error>
#else
#include <cstring>
#endif
#endif

namespace microfmt {

namespace detail {

#if defined(MICROFMT_KERNEL)
// Kernel mode: Forward declaration. The kernel developer provides the actual
// implementation linking against kernel logging/error facilities.
void write_errno_string(const sink &out, int value) noexcept;
#else
// User-space / Host mode: Implemented using system_error or strerror_r
inline void write_errno_string(const sink &out, int value) noexcept {
#if MICROFMT_USE_SYSTEM_ERROR
  std::error_code ec(value, std::system_category());
  std::string msg = ec.message();
  if (!msg.empty()) {
    out.write(msg);
  } else {
    out.write("Unknown error");
  }
#else
  char buf[256];
  buf[0] = '\0';

#if defined(_WIN32)
  if (strerror_s(buf, sizeof(buf), value) != 0) {
    std::strncpy(buf, "Unknown error", sizeof(buf));
  }
#elif defined(__APPLE__) || (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L && !defined(_GNU_SOURCE))
  if (strerror_r(value, buf, sizeof(buf)) != 0) {
    std::strncpy(buf, "Unknown error", sizeof(buf));
  }
#elif defined(_GNU_SOURCE) || defined(__GLIBC__)
  const char *err_msg = strerror_r(value, buf, sizeof(buf));
  if (err_msg) {
    out.write(err_msg);
    return;
  }
#else
  if (const char *msg = std::strerror(value)) {
    out.write(msg);
    return;
  }
#endif

  if (buf[0] != '\0') {
    out.write(buf);
  } else {
    out.write("Unknown error");
  }
#endif
}
#endif

} // namespace detail

struct posix_errno {
  int value;
};

constexpr inline posix_errno format_errno(int val) noexcept { return {val}; }

inline posix_errno current_errno() noexcept { return {errno}; }

template <> struct formatter<posix_errno> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const posix_errno &e, const sink &out) const noexcept {
    // Delegate to detail implementation (forward-declared in kernel, inline in host)
    detail::write_errno_string(out, e.value);

    // Append the numeric code format: " (os:13)"
    out.write(" (os:");

    formatter<int> int_fmt;
    int_fmt.format(e.value, out);

    out.write(")");
  }
};

} // namespace microfmt