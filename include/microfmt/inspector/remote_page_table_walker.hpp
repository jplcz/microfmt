// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_page_table_walker.hpp
 * @brief Bounded architecture-neutral remote page-table walking. */

#include "../array.hpp"
#include "../expected.hpp"
#include "../span.hpp"
#include "../value_ptr.hpp"
#include "address_space.hpp"
#include "address_translator.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace microfmt {

/**
 * @brief Address-index and entry-width description for one table level.
 */
struct remote_page_table_level {
  uint8_t index_shift{0};
  uint8_t index_bits{0};
  uint8_t entry_size{0};
};

/**
 * @brief Meaning assigned to a decoded page-table entry.
 */
enum class remote_page_table_entry_kind : uint8_t {
  invalid,
  next_table,
  leaf,
};

/**
 * @brief Architecture callback output for one decoded entry.
 */
struct remote_page_table_decoded_entry {
  remote_page_table_entry_kind kind{remote_page_table_entry_kind::invalid};
  uintptr_t output_address{0};
  size_t page_size{0};
  uint8_t space_id{0};
  bool is_secure{false};
  bool readable{false};
  bool writable{false};
  bool executable{false};
  bool user_accessible{false};
};

/**
 * @brief Callback table describing an architecture's page-table format.
 */
struct remote_page_table_callbacks {
  size_t level_count{0};

  bool (*describe_level)(const void *state, size_t level,
                         remote_page_table_level &description) noexcept{
      nullptr};

  bool (*decode_entry)(const void *state, size_t level, uint64_t raw_entry,
                       remote_page_table_decoded_entry &entry) noexcept{
      nullptr};
};

/**
 * @brief Borrowed architecture layout used by a remote page-table walker.
 */
class RELOCO_POINTER remote_page_table_layout_ref {
public:
  constexpr remote_page_table_layout_ref() noexcept = default;

  constexpr explicit remote_page_table_layout_ref(
      remote_page_table_callbacks callbacks) noexcept
      : callbacks_(callbacks) {}

  template <typename State>
  constexpr remote_page_table_layout_ref(
      const State &state RELOCO_LIFETIMEBOUND,
      remote_page_table_callbacks callbacks) noexcept
      : state_(&state), callbacks_(callbacks) {}

  [[nodiscard]] constexpr size_t level_count() const noexcept {
    return callbacks_.level_count;
  }

  [[nodiscard]] bool
  describe_level(size_t level,
                 remote_page_table_level &description) const noexcept {
    return callbacks_.describe_level && level < callbacks_.level_count &&
           callbacks_.describe_level(state_.get(), level, description);
  }

  [[nodiscard]] bool
  decode_entry(size_t level, uint64_t raw_entry,
               remote_page_table_decoded_entry &entry) const noexcept {
    return callbacks_.decode_entry && level < callbacks_.level_count &&
           callbacks_.decode_entry(state_.get(), level, raw_entry, entry);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return callbacks_.level_count != 0 && callbacks_.describe_level &&
           callbacks_.decode_entry;
  }

private:
  value_ptr<const void> state_{};
  remote_page_table_callbacks callbacks_{};
};

/**
 * @brief One recorded level from a remote page-table walk.
 */
struct remote_page_table_walk_step {
  size_t level{0};
  uintptr_t table_address{0};
  uintptr_t entry_address{0};
  uint64_t raw_entry{0};
  remote_page_table_decoded_entry decoded{};
};

/**
 * @brief Caller-owned bounded storage for page-table walk diagnostics.
 */
class remote_page_table_walk_trace {
public:
  constexpr explicit remote_page_table_walk_trace(
      span<remote_page_table_walk_step> storage) noexcept
      : storage_(storage) {}

  template <size_t N>
  constexpr explicit remote_page_table_walk_trace(
      remote_page_table_walk_step (&storage)[N]) noexcept
      : storage_(storage, N) {}

  [[nodiscard]] constexpr
  span<const remote_page_table_walk_step> steps() const noexcept {
    return span<const remote_page_table_walk_step>(storage_.data(), size_);
  }

  [[nodiscard]] constexpr size_t size() const noexcept { return size_; }

  [[nodiscard]] constexpr bool truncated() const noexcept {
    return truncated_;
  }

private:
  friend class remote_page_table_walker;

  constexpr void reset() noexcept {
    size_ = 0;
    truncated_ = false;
  }

  constexpr void append(const remote_page_table_walk_step &step) noexcept {
    if (size_ < storage_.size()) {
      storage_[size_++] = step;
    } else {
      truncated_ = true;
    }
  }

  span<remote_page_table_walk_step> storage_{};
  size_t size_{0};
  bool truncated_{false};
};

/**
 * @brief Errors produced while walking remote page tables.
 */
enum class remote_page_table_walk_error : uint8_t {
  invalid_physical_space,
  invalid_layout,
  invalid_root,
  invalid_level,
  unsupported_entry_size,
  address_overflow,
  read_failed,
  decode_failed,
  invalid_entry,
  unexpected_next_table,
  invalid_leaf,
};

/**
 * @brief Final virtual-to-physical result from a remote page-table walk.
 */
struct remote_page_table_walk_result {
  uintptr_t physical_address{0};
  size_t page_size{0};
  translation_attributes attributes{};
};

/**
 * @brief Bounded remote page-table walker using physical memory access.
 */
class remote_page_table_walker {
public:
  constexpr remote_page_table_walker(
      address_space_ref physical_space,
      remote_page_table_layout_ref layout) noexcept
      : physical_space_(physical_space), layout_(layout) {}

  /**
   * @brief Walks page tables rooted at a physical address.
   *
   * The trace is reset before walking and retains all recorded steps on both
   * success and failure. Walking continues when trace storage is exhausted;
   * `trace.truncated()` then reports the loss of diagnostic steps.
   */
  [[nodiscard]] expected<remote_page_table_walk_result,
                         remote_page_table_walk_error>
  walk(uintptr_t root_table_address, uintptr_t virtual_address,
       remote_page_table_walk_trace &trace) const noexcept {
    trace.reset();
    if (!physical_space_)
      return unexpected(
          remote_page_table_walk_error::invalid_physical_space);
    if (!layout_)
      return unexpected(remote_page_table_walk_error::invalid_layout);
    if (root_table_address == 0)
      return unexpected(remote_page_table_walk_error::invalid_root);

    uintptr_t table_address = root_table_address;
    const size_t level_count = layout_.level_count();

    for (size_t level = 0; level < level_count; ++level) {
      remote_page_table_level description{};
      if (!layout_.describe_level(level, description) ||
          description.index_bits == 0 || description.index_bits >= 64 ||
          description.index_shift >=
              static_cast<uint8_t>(sizeof(uintptr_t) * 8)) {
        return unexpected(remote_page_table_walk_error::invalid_level);
      }
      if (description.entry_size != 1 && description.entry_size != 2 &&
          description.entry_size != 4 && description.entry_size != 8) {
        return unexpected(
            remote_page_table_walk_error::unsupported_entry_size);
      }

      const uint64_t index_mask =
          (UINT64_C(1) << description.index_bits) - UINT64_C(1);
      const uint64_t index =
          (static_cast<uint64_t>(virtual_address) >>
           description.index_shift) &
          index_mask;
      if (index >
          std::numeric_limits<uintptr_t>::max() / description.entry_size) {
        return unexpected(remote_page_table_walk_error::address_overflow);
      }
      const uintptr_t entry_offset =
          static_cast<uintptr_t>(index) * description.entry_size;
      if (table_address >
          std::numeric_limits<uintptr_t>::max() - entry_offset) {
        return unexpected(remote_page_table_walk_error::address_overflow);
      }
      const uintptr_t entry_address = table_address + entry_offset;

      array<std::byte, 8> entry_bytes{};
      auto read_result = physical_space_.read_bytes(
          entry_address, entry_bytes.data(), description.entry_size);
      if (!read_result)
        return unexpected(remote_page_table_walk_error::read_failed);

      uint64_t raw_entry = 0;
      for (size_t byte = 0; byte < description.entry_size; ++byte) {
        raw_entry |=
            static_cast<uint64_t>(
                static_cast<unsigned char>(entry_bytes[byte]))
            << (byte * 8);
      }

      remote_page_table_decoded_entry decoded{};
      if (!layout_.decode_entry(level, raw_entry, decoded))
        return unexpected(remote_page_table_walk_error::decode_failed);

      trace.append(remote_page_table_walk_step{
          level, table_address, entry_address, raw_entry, decoded});

      if (decoded.kind == remote_page_table_entry_kind::invalid)
        return unexpected(remote_page_table_walk_error::invalid_entry);

      if (decoded.kind == remote_page_table_entry_kind::next_table) {
        if (level + 1 == level_count || decoded.output_address == 0)
          return unexpected(
              remote_page_table_walk_error::unexpected_next_table);
        table_address = decoded.output_address;
        continue;
      }

      if (decoded.kind != remote_page_table_entry_kind::leaf ||
          decoded.page_size == 0 ||
          (decoded.page_size & (decoded.page_size - 1)) != 0 ||
          decoded.output_address >
              std::numeric_limits<uintptr_t>::max() -
                  (virtual_address & (decoded.page_size - 1))) {
        return unexpected(remote_page_table_walk_error::invalid_leaf);
      }

      const uintptr_t physical_address =
          decoded.output_address +
          (virtual_address & (decoded.page_size - 1));
      translation_attributes attributes{
          physical_address,
          decoded.space_id,
          decoded.is_secure,
          decoded.readable,
          decoded.writable,
          decoded.executable,
          decoded.user_accessible};
      return remote_page_table_walk_result{
          physical_address, decoded.page_size, attributes};
    }

    return unexpected(remote_page_table_walk_error::invalid_entry);
  }

private:
  address_space_ref physical_space_{};
  remote_page_table_layout_ref layout_{};
};

} // namespace microfmt
