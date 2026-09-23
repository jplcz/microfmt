// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file fallible_address_space.hpp @brief Fallible local address space with signal-safe fault recovery and
 * customizable TLS. */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

// clang-format off
#include "../microfmt.hpp"
#include "../detail/tls_provider.hpp"
#include "address_space.hpp"
// clang-format on

/*
 * NOTE: This file works only on targets with support for POSIX signals
 */

namespace microfmt {

/**
 * @brief Tag identifying the fallible local address space.
 */
struct fallible_local_space_tag {};

namespace detail {

/**
 * @brief Context holding the jump buffer and active state for signal-safe memory fault recovery.
 */
struct MICROFMT_API_CLASS fault_recovery_context {
  sigjmp_buf env;
  bool active{false};
};

/**
 * @brief Unique tag to isolate the fallible address space's TLS storage slot.
 */
struct fallible_space_tls_tag {};

// Convenience alias for the tag-differentiated TLS state
using fallible_tls = tls_provider<fault_recovery_context *, fallible_space_tls_tag > ;

inline void fallible_signal_handler(int sig, siginfo_t *, void *) noexcept {
  auto *ctx = fallible_tls::get();
  if (ctx && ctx->active) {
    siglongjmp(ctx->env, sig);
  }

  // If no fallible operation is active, restore default signal handler and re-raise
  struct sigaction sa{};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

inline bool install_fallible_handlers() noexcept {
  static bool initialized = []() {
    struct sigaction sa{};
    sa.sa_sigaction = fallible_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    return true;
  }();
  return initialized;
}

} // namespace detail

/**
 * @brief Traits for @ref fallible_local_space_tag providing fault-safe memory reads and writes.
 */
template <> struct address_space_traits<fallible_local_space_tag> {
  using context_type = void;

  static bool read_bytes(uintptr_t addr, void *dest, size_t size) noexcept {
    if (addr == 0)
      return false;
    if (size == 0)
      return true;

    detail::install_fallible_handlers();

    detail::fault_recovery_context fault_ctx{};
    fault_ctx.active = true;

    // Nesting-safe context management via the tag-differentiated TLS state
    auto *old_ctx = detail::fallible_tls::get();
    detail::fallible_tls::set(&fault_ctx);

    if (sigsetjmp(fault_ctx.env, 1) != 0) {
      detail::fallible_tls::set(old_ctx);
      return false;
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    std::memcpy(dest, reinterpret_cast<const void *>(addr), size);
    RELOCO_END_UNSAFE_BUFFER_USAGE;

    detail::fallible_tls::set(old_ctx);
    return true;
  }

  static bool write_bytes(uintptr_t addr, const void *src, size_t size) noexcept {
    if (addr == 0)
      return false;
    if (size == 0)
      return true;

    detail::install_fallible_handlers();

    detail::fault_recovery_context fault_ctx{};
    fault_ctx.active = true;

    auto *old_ctx = detail::fallible_tls::get();
    detail::fallible_tls::set(&fault_ctx);

    if (sigsetjmp(fault_ctx.env, 1) != 0) {
      detail::fallible_tls::set(old_ctx);
      return false;
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    std::memcpy(reinterpret_cast<void *>(addr), src, size);
    RELOCO_END_UNSAFE_BUFFER_USAGE;

    detail::fallible_tls::set(old_ctx);
    return true;
  }

  static bool read_string(uintptr_t addr, char *dest, size_t max_len, size_t &out_len, bool &null_term) noexcept {
    if (addr == 0)
      return false;

    detail::install_fallible_handlers();

    detail::fault_recovery_context fault_ctx{};
    fault_ctx.active = true;

    auto *old_ctx = detail::fallible_tls::get();
    detail::fallible_tls::set(&fault_ctx);

    if (sigsetjmp(fault_ctx.env, 1) != 0) {
      detail::fallible_tls::set(old_ctx);
      return false;
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    const char *src = reinterpret_cast<const char *>(addr);
    size_t i = 0;
    while (i < max_len) {
      dest[i] = src[i];
      if (dest[i] == '\0') {
        out_len = i;
        null_term = true;
        detail::fallible_tls::set(old_ctx);
        return true;
      }
      ++i;
    }
    RELOCO_END_UNSAFE_BUFFER_USAGE;

    detail::fallible_tls::set(old_ctx);
    out_len = max_len;
    null_term = false;
    return true;
  }
};

} // namespace microfmt
