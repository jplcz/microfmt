// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file tagged_log_sink_base.hpp
 * @brief Shared base for log sink backends that tag each record with a
 * fixed-capacity, null-terminated string (e.g. Android logcat's tag,
 * Tizen DLOG's tag).
 *
 * Every such backend needs the exact same two things: a truncating
 * `set_tag()` that copies a caller-supplied default tag into fixed
 * storage, and a per-record `resolve_tag()` that prefers the originating
 * logger's own name (also truncated into a caller-supplied scratch
 * buffer) so records from different loggers stay distinguishable in
 * platform log filters, falling back to the sink's configured default tag
 * when the record carries none. Factoring both into `tagged_log_sink_base`
 * keeps that logic (and its `RELOCO_UNSAFE_BUFFER_USAGE` bookkeeping)
 * written and reviewed exactly once.
 */

#include "../../microfmt.hpp"
#include <cstddef>
#include <string_view>

namespace microfmt::log::detail {

/**
 * @brief CRTP-free base providing fixed-capacity tag storage and
 * truncating tag resolution, shared by tag-per-record log sink backends.
 *
 * @tparam TagCapacity Bytes of storage for the default tag, including the
 * trailing null terminator.
 */
template <std::size_t TagCapacity> class tagged_log_sink_base {
  static_assert(TagCapacity > 0, "Tag capacity must be at least 1 byte");

public:
  /**
   * @brief Replaces the default tag used for records whose logger has no
   * name, truncating to fit if necessary.
   */
  void set_tag(microfmt::string_view tag) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    tag_size_ = tag.size() < TagCapacity - 1 ? tag.size() : TagCapacity - 1;
    for (std::size_t i = 0; i < tag_size_; ++i) {
      tag_[i] = tag[i];
    }
    tag_[tag_size_] = '\0';

    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }

protected:
  constexpr tagged_log_sink_base() noexcept = default;
  explicit tagged_log_sink_base(microfmt::string_view tag) noexcept { set_tag(tag); }

  /**
   * @brief Resolves the tag to use for a record whose originating logger is
   * named @p logger_name: that name (truncated into @p scratch) if
   * non-empty, otherwise this sink's configured default tag.
   */
  [[nodiscard]] microfmt::string_view resolve_tag(microfmt::string_view logger_name,
                                                   char (&scratch)[TagCapacity]) const noexcept {
    if (logger_name.empty()) {
      return microfmt::string_view(tag_, tag_size_);
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    const std::size_t n = logger_name.size() < TagCapacity - 1 ? logger_name.size() : TagCapacity - 1;
    for (std::size_t i = 0; i < n; ++i) {
      scratch[i] = logger_name[i];
    }
    scratch[n] = '\0';

    RELOCO_END_UNSAFE_BUFFER_USAGE;
    return microfmt::string_view(scratch, n);
  }

  char tag_[TagCapacity]{};
  std::size_t tag_size_{0};
};

} // namespace microfmt::log::detail
