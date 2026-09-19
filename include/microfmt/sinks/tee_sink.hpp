// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tee_sink.hpp @brief Fan-out sink that writes to multiple destinations. */

#include "../microfmt.hpp"
#include <cstddef>
#include <string_view>

namespace microfmt {

// ============================================================================
// Tee Sink (Multi-target broadcast)
// ============================================================================

template <size_t N = 2> class tee_sink {
  static_assert(N > 0, "tee_sink must have at least one target sink");

public:
  template <typename... Sinks,
            std::enable_if_t<(sizeof...(Sinks) <= N), int> = 0>
  explicit constexpr tee_sink(Sinks... sinks) noexcept
      : targets_{sinks...}, count_(sizeof...(Sinks)) {}

  // Non-copyable
  tee_sink(const tee_sink &) = delete;
  tee_sink &operator=(const tee_sink &) = delete;
  tee_sink(tee_sink &&) noexcept = default;
  tee_sink &operator=(tee_sink &&) noexcept = default;

  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, [](void *ctx, microfmt::string_view sv) noexcept {
                  static_cast<tee_sink *>(ctx)->write(sv);
                }};
  }

  bool add_target(sink s) noexcept {
    if (count_ >= N) {
      return false;
    }
    targets_[count_++] = s;
    return true;
  }

  void put(char c) const noexcept {
    for (size_t i = 0; i < count_; ++i) {
      targets_[i].put(c);
    }
  }

  void write(microfmt::string_view sv) const noexcept {
    for (size_t i = 0; i < count_; ++i) {
      targets_[i].write(sv);
    }
  }

  [[nodiscard]] constexpr size_t target_count() const noexcept {
    return count_;
  }

private:
  microfmt::array<sink, N> targets_{};
  size_t count_{0};
};

// Convenience factory deduction helper
template <typename... Sinks>
[[nodiscard]] constexpr auto make_tee(Sinks... sinks) noexcept {
  return tee_sink<sizeof...(Sinks)>{sinks...};
}

} // namespace microfmt