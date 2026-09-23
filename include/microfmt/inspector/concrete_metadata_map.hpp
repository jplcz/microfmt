// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file concrete_metadata_map.hpp
 * @brief Concrete mutable metadata map with explicit view state. */

#include "../reloco.hpp"
#include "metadata_map.hpp"
#include <type_traits>

namespace microfmt {

/**
 * @brief Concrete mutable metadata map backed by an external span storage.
 *
 * Enforces compile-time lifetime safety via value_ref and requires an explicit
 * stack state struct to generate a read-only metadata_map view.
 */
class MICROFMT_API_CLASS RELOCO_OWNER concrete_metadata_map {
public:
  /**
   * @brief Constructs a concrete metadata map over an external span buffer.
   * @param storage Caller-owned span of property_entry structures for backing memory.
   */
  explicit constexpr concrete_metadata_map(span<property_entry> storage) noexcept : m_storage(storage), m_count(0) {}

  /**
   * @brief Stores or updates a property safely with type erasure.
   *
   * @tparam T Inferred type of the referenced property.
   * @param key Property key string view.
   * @param ref Lifetime-bound wrapper ensuring the object is not a temporary.
   * @return `true` if stored successfully, `false` if the backing span is full.
   */
  template <typename T> constexpr bool set(microfmt::string_view key, value_ref<T> ref) noexcept {
    const T *val_ptr = ref.get();

    // Update existing key if found
    for (std::size_t i = 0; i < m_count; ++i) {
      if (m_storage[i].key == key) {
        m_storage[i].val_ptr = static_cast<const void *>(val_ptr);
        m_storage[i].print_fn = [](const void *ptr, const sink &out) noexcept {
          using DecayedT = std::decay_t<T>;
          formatter<DecayedT> fmt{};
          fmt.format(*static_cast<const DecayedT *>(ptr), out);
        };
        return true;
      }
    }

    // Insert new entry if storage span capacity permits
    if (m_count < m_storage.size()) {
      m_storage[m_count] =
          property_entry{key, static_cast<const void *>(val_ptr), [](const void *ptr, const sink &out) noexcept {
                           using DecayedT = std::decay_t<T>;
                           formatter<DecayedT> fmt{};
                           fmt.format(*static_cast<const DecayedT *>(ptr), out);
                         }};
      ++m_count;
      return true;
    }

    return false; // Backing span capacity exceeded
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_count; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept { return m_storage.size(); }
  [[nodiscard]] constexpr bool empty() const noexcept { return m_count == 0; }

  /**
   * @brief Required stack state structure to drive iteration safely.
   */
  struct span_iteration_state {
    const property_entry *current{nullptr};
    const property_entry *end{nullptr};
  };

  /**
   * @brief Exclusively creates a type-erased metadata_map view using caller-provided stack state.
   *
   * @param state Reference to a stack-allocated span_iteration_state object.
   * @return A lightweight, zero-allocation metadata_map view.
   */
  [[nodiscard]] constexpr metadata_map make_view(span_iteration_state &state) const & noexcept RELOCO_LIFETIMEBOUND {
    state.current = m_storage.data();

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
    state.end = m_storage.data() + m_count;

    return metadata_map(&state, [](void *ctx, property_entry &out) noexcept -> bool {
      auto *s = static_cast<span_iteration_state *>(ctx);
      if (!s || s->current >= s->end) {
        return false;
      }
      out = *s->current++;

      return true;
    });
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }

  metadata_map make_view(span_iteration_state &) const && = delete;

private:
  span<property_entry> m_storage;
  std::size_t m_count{0};
};

} // namespace microfmt