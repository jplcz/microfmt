// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "address_space.hpp"
#include "dwarf_registers.hpp"
#include "../microfmt.hpp"
#include "../span.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace microfmt {

/**
 * @brief Type-erased vtable for querying architecture-specific registers
 *        by DWARF register index from a debugging execution context frame.
 */
struct register_context_vtable {
  /**
   * @brief Reads a target register value by its DWARF index.
   *
   * @param state Opaque pointer to underlying target thread/frame context state
   * @param space Address space reference for memory-mapped register states if
   * needed
   * @param dwarf_reg_index Architectural DWARF register number (from
   * dwarf_registers.hpp)
   * @param out_val Pointer to destination storage buffer
   * @param val_size Size in bytes of the destination buffer
   * @return true if successfully read, false if unavailable or out of bounds.
   */
  bool (*read_register)(const void *state, address_space_ref space,
                        uint32_t dwarf_reg_index, void *out_val,
                        size_t val_size) noexcept;

  /**
   * @brief Writes a target register value by its DWARF index.
   *
   * @param state Opaque pointer to underlying target thread/frame context state
   * @param space Address space reference
   * @param dwarf_reg_index Architectural DWARF register number
   * @param in_val Pointer to source storage buffer containing the new value
   * @param val_size Size in bytes of the source buffer
   * @return true if successfully written, false if read-only, unavailable, or
   * out of bounds.
   */
  bool (*write_register)(void *state, address_space_ref space,
                         uint32_t dwarf_reg_index, const void *in_val,
                         size_t val_size) noexcept;
};

/**
 * @brief Zero-allocation, type-erased handle for inspecting and modifying CPU
 * registers from a frame context.
 */
class MICROFMT_POINTER register_context_ref {
public:
  constexpr register_context_ref() noexcept = default;

  template <typename State>
  constexpr register_context_ref(
                                 State *state_ptr MICROFMT_LIFETIMEBOUND
                                     MICROFMT_LIFETIME_CAPTURE_BY_THIS,
                                 register_context_vtable vtable,
                                 address_space_ref space,
                                 span<std::byte> scratch
                                     MICROFMT_LIFETIMEBOUND
                                         MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : state_(state_ptr), vtable_(vtable), space_(space), scratch_(scratch) {}

  template <typename State>
  constexpr register_context_ref(
                                 const State *state_ptr
                                     MICROFMT_LIFETIMEBOUND
                                         MICROFMT_LIFETIME_CAPTURE_BY_THIS,
                                 register_context_vtable vtable,
                                 address_space_ref space,
                                 span<std::byte> scratch
                                     MICROFMT_LIFETIMEBOUND
                                         MICROFMT_LIFETIME_CAPTURE_BY_THIS) noexcept
      : state_(const_cast<State *>(state_ptr)), vtable_(vtable), space_(space),
        scratch_(scratch) {}

  [[nodiscard]] constexpr bool is_null() const noexcept {
    return !state_ || (!vtable_.read_register && !vtable_.write_register);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !is_null();
  }

  /**
   * @brief Reads a register directly into a typed integer or float variable.
   */
  template <typename T>
  [[nodiscard]] bool read(uint32_t dwarf_reg_index,
                          T &out_value) const noexcept {
    if (!vtable_.read_register)
      return false;
    return vtable_.read_register(state_.get(), space_, dwarf_reg_index,
                                 &out_value, sizeof(T));
  }

  /**
   * @brief Reads raw register bytes into a buffer.
   */
  [[nodiscard]] bool read_raw(uint32_t dwarf_reg_index, void *dest,
                              size_t size) const noexcept {
    if (!vtable_.read_register)
      return false;
    return vtable_.read_register(state_.get(), space_, dwarf_reg_index, dest,
                                 size);
  }

  /**
   * @brief Writes a typed value into a register by its DWARF index.
   */
  template <typename T>
  [[nodiscard]] bool write(uint32_t dwarf_reg_index,
                           const T &value) const noexcept {
    if (!vtable_.write_register)
      return false;
    return vtable_.write_register(state_.get(), space_, dwarf_reg_index,
                                  &value, sizeof(T));
  }

  /**
   * @brief Writes raw bytes from a source buffer into a register.
   */
  [[nodiscard]] bool write_raw(uint32_t dwarf_reg_index, const void *src,
                               size_t size) const noexcept {
    if (!vtable_.write_register)
      return false;
    return vtable_.write_register(state_.get(), space_, dwarf_reg_index, src,
                                  size);
  }

  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr span<std::byte>
  scratch() const noexcept MICROFMT_LIFETIMEBOUND {
    return scratch_;
  }

private:
  value_ptr<void> state_{};
  register_context_vtable vtable_{};
  address_space_ref space_{};
  span<std::byte> scratch_{};
};

namespace detail {

template <typename T> struct register_member_pointer_traits;

template <typename Value, typename State>
struct register_member_pointer_traits<Value State::*> {
  using state_type = State;
  using value_type = Value;
};

template <typename Field, typename State, typename = void>
struct has_mutable_register_field_get : std::false_type {};

template <typename Field, typename State>
struct has_mutable_register_field_get<
    Field, State,
    std::void_t<decltype(Field::get(std::declval<State &>(),
                                    std::declval<uint32_t>()))>>
    : std::is_convertible<
          decltype(Field::get(std::declval<State &>(),
                              std::declval<uint32_t>())),
          typename Field::value_type *> {};

template <typename Field, typename State, typename = void>
struct has_register_field_read : std::false_type {};

template <typename Field, typename State>
struct has_register_field_read<
    Field, State,
    std::void_t<decltype(Field::read(
        std::declval<const State &>(), std::declval<address_space_ref>(),
        std::declval<uint32_t>(),
        std::declval<typename Field::value_type &>()))>>
    : std::is_same<
          decltype(Field::read(
              std::declval<const State &>(),
              std::declval<address_space_ref>(), std::declval<uint32_t>(),
              std::declval<typename Field::value_type &>())),
          bool> {};

template <typename Field, typename State, typename = void>
struct has_register_field_write : std::false_type {};

template <typename Field, typename State>
struct has_register_field_write<
    Field, State,
    std::void_t<decltype(Field::write(
        std::declval<State &>(), std::declval<address_space_ref>(),
        std::declval<uint32_t>(),
        std::declval<const typename Field::value_type &>()))>>
    : std::is_same<
          decltype(Field::write(
              std::declval<State &>(), std::declval<address_space_ref>(),
              std::declval<uint32_t>(),
              std::declval<const typename Field::value_type &>())),
          bool> {};

} // namespace detail

/**
 * @brief Compile-time register-field trait backed by a pointer to member.
 *
 * Multiple register indexes may alias the same member. For nested, indexed,
 * or mode-dependent layouts, provide a custom trait with the same
 * `state_type`, `value_type`, `matches`, and `get` surface.
 *
 * @tparam Member Pointer to the bound state member.
 * @tparam Registers Register indexes mapped to the member.
 */
template <auto Member, uint32_t... Registers>
struct register_member_field {
  using member_traits =
      detail::register_member_pointer_traits<decltype(Member)>;
  using state_type = typename member_traits::state_type;
  using value_type = typename member_traits::value_type;

  static_assert(sizeof...(Registers) != 0,
                "a register member field must bind at least one register");
  static_assert(std::is_trivially_copyable<value_type>::value,
                "register fields must be trivially copyable");

  [[nodiscard]] static constexpr bool matches(uint32_t index) noexcept {
    return ((index == Registers) || ...);
  }

  [[nodiscard]] static constexpr const value_type *
  get(const state_type &state, uint32_t) noexcept {
    return &(state.*Member);
  }

  [[nodiscard]] static constexpr value_type *
  get(state_type &state, uint32_t) noexcept {
    return &(state.*Member);
  }
};

/**
 * @brief Compile-time field trait with state-dependent member selection.
 *
 * The selectors can inspect the complete source register set before returning
 * the member that represents the requested register. This supports banked
 * registers and ABI choices such as ARM frame pointer selection from CPSR.T.
 *
 * @tparam State Bound register structure.
 * @tparam Value Selected register value type.
 * @tparam Selector Stateless function object returning a pointer into the
 * state. A templated call operator can preserve the state's constness without
 * separate const and mutable selector overloads. Returning only
 * `const value_type *` makes the field read-only.
 * @tparam Registers Register indexes handled by the selectors.
 */
template <typename State, typename Value, typename Selector,
          uint32_t... Registers>
struct register_selected_field {
  using state_type = State;
  using value_type = Value;

  static_assert(sizeof...(Registers) != 0,
                "a selected register field must bind at least one register");
  static_assert(std::is_trivially_copyable<value_type>::value,
                "register fields must be trivially copyable");
  static_assert(std::is_nothrow_default_constructible<Selector>::value,
                "selector must be nothrow default constructible");
  static_assert(
      std::is_nothrow_invocable_r<const value_type *, Selector,
                                  const state_type &, uint32_t>::value,
      "selector must be noexcept and return a value_type pointer");

  [[nodiscard]] static constexpr bool matches(uint32_t index) noexcept {
    return ((index == Registers) || ...);
  }

  [[nodiscard]] static constexpr const value_type *
  get(const state_type &state, uint32_t index) noexcept {
    return Selector{}(state, index);
  }

  [[nodiscard]] static constexpr auto get(state_type &state,
                                          uint32_t index) noexcept
      -> decltype(Selector{}(state, index)) {
    return Selector{}(state, index);
  }
};

/**
 * @brief Compile-time register field queried through typed callbacks.
 *
 * This supports registers that are not stored in the bound state, including
 * values read directly from hardware. Passing `nullptr` as @p WriteCallback
 * creates a read-only field.
 *
 * @tparam State Bound context state.
 * @tparam Value Register value type.
 * @tparam ReadCallback Nothrow callback that reads a register value.
 * @tparam WriteCallback Nothrow callback that writes a register value, or
 * `nullptr`.
 * @tparam Registers Register indexes handled by the callbacks.
 */
template <typename State, typename Value, auto ReadCallback,
          auto WriteCallback, uint32_t... Registers>
struct register_callback_field {
  using state_type = State;
  using value_type = Value;

  static_assert(sizeof...(Registers) != 0,
                "a callback register field must bind at least one register");
  static_assert(std::is_trivially_copyable<value_type>::value,
                "register fields must be trivially copyable");
  static_assert(std::is_nothrow_default_constructible<value_type>::value,
                "callback register fields must be nothrow default "
                "constructible");
  static_assert(
      std::is_nothrow_invocable_r<bool, decltype(ReadCallback),
                                  const state_type &, address_space_ref,
                                  uint32_t, value_type &>::value,
      "read callback must be noexcept and return bool");
  static_assert(
      WriteCallback == nullptr ||
          std::is_nothrow_invocable_r<bool, decltype(WriteCallback),
                                      state_type &, address_space_ref,
                                      uint32_t, const value_type &>::value,
      "write callback must be nullptr or noexcept and return bool");

  [[nodiscard]] static constexpr bool matches(uint32_t index) noexcept {
    return ((index == Registers) || ...);
  }

  [[nodiscard]] static bool read(const state_type &state,
                                 address_space_ref space, uint32_t index,
                                 value_type &value) noexcept {
    return ReadCallback(state, space, index, value);
  }

  template <auto Callback = WriteCallback,
            typename std::enable_if<Callback != nullptr, int>::type = 0>
  [[nodiscard]] static bool write(state_type &state, address_space_ref space,
                                  uint32_t index,
                                  const value_type &value) noexcept {
    return Callback(state, space, index, value);
  }
};

/**
 * @brief Typed owner that exposes a C/C++ register structure as a
 * @ref register_context_ref.
 *
 * Field traits are checked in declaration order. The first matching trait
 * handles the register. Unmatched indexes are delegated to the optional
 * fallback context, allowing mapped contexts to be chained.
 *
 * A custom field trait must define:
 *
 * - `state_type` and trivially-copyable `value_type`;
 * - `static bool matches(uint32_t) noexcept`;
 * - either `get` functions returning pointers to stored values, or typed
 *   `read` and optional `write` functions;
 * - omit mutable `get` or `write` to make the field read-only.
 *
 * Callback fields receive the wrapper's address space and may query hardware
 * instead of returning a value stored in the bound state.
 */
template <typename State, typename... FieldTraits>
class register_context_ref_with {
public:
  static_assert(sizeof...(FieldTraits) != 0,
                "at least one register field trait is required");
  static_assert(
      (std::is_same<typename FieldTraits::state_type, State>::value && ...),
      "all register field traits must bind the wrapper state type");
  static_assert(
      (std::is_trivially_copyable<typename FieldTraits::value_type>::value &&
       ...),
      "register field values must be trivially copyable");

  constexpr register_context_ref_with(
      State &state MICROFMT_LIFETIMEBOUND, address_space_ref space,
      span<std::byte> scratch MICROFMT_LIFETIMEBOUND,
      register_context_ref fallback = {}) noexcept
      : state_(&state), writable_(true), space_(space), scratch_(scratch),
        fallback_(fallback) {}

  constexpr register_context_ref_with(
      const State &state MICROFMT_LIFETIMEBOUND, address_space_ref space,
      span<std::byte> scratch MICROFMT_LIFETIMEBOUND,
      register_context_ref fallback = {}) noexcept
      : state_(const_cast<State *>(&state)), writable_(false), space_(space),
        scratch_(scratch), fallback_(fallback) {}

  /**
   * @brief Creates a type-erased reference borrowing this wrapper.
   *
   * This wrapper, its state, scratch storage, address-space context, and any
   * fallback context must outlive the returned reference.
   */
  [[nodiscard]] constexpr register_context_ref
  ref() noexcept MICROFMT_LIFETIMEBOUND {
    return register_context_ref(this, vtable_, space_, scratch_);
  }

  [[nodiscard]] constexpr operator register_context_ref() noexcept
      MICROFMT_LIFETIMEBOUND {
    return ref();
  }

private:
  template <typename Field>
  [[nodiscard]] bool try_read(uint32_t index, void *destination, size_t size,
                              bool &handled) const noexcept {
    if (handled || !Field::matches(index))
      return false;

    handled = true;
    if (!destination || size != sizeof(typename Field::value_type))
      return false;

    if constexpr (detail::has_register_field_read<Field, State>::value) {
      typename Field::value_type value{};
      if (!Field::read(*state_, space_, index, value))
        return false;
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(destination, &value, sizeof(value));
      MICROFMT_END_UNSAFE_BUFFER_USAGE;
      return true;
    } else {
      const auto *value = Field::get(*state_, index);
      if (!value)
        return false;
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(destination, value, sizeof(*value));
      MICROFMT_END_UNSAFE_BUFFER_USAGE;
      return true;
    }
  }

  template <typename Field>
  [[nodiscard]] bool try_write(uint32_t index, const void *source, size_t size,
                               bool &handled) noexcept {
    if (handled || !Field::matches(index))
      return false;

    handled = true;
    if (!writable_ || !source || size != sizeof(typename Field::value_type))
      return false;

    if constexpr (detail::has_register_field_write<Field, State>::value) {
      typename Field::value_type value{};
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(&value, source, sizeof(value));
      MICROFMT_END_UNSAFE_BUFFER_USAGE;
      return Field::write(*state_, space_, index, value);
    } else if constexpr (detail::has_mutable_register_field_get<Field,
                                                                 State>::value) {
      auto *value = Field::get(*state_, index);
      if (!value)
        return false;
      MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(value, source, sizeof(*value));
      MICROFMT_END_UNSAFE_BUFFER_USAGE;
      return true;
    } else {
      return false;
    }
  }

  [[nodiscard]] bool read(uint32_t index, void *destination,
                          size_t size) const noexcept {
    bool handled = false;
    bool result = false;
    using expand = int[];
    static_cast<void>(
        expand{0, (handled ? 0
                           : (result = try_read<FieldTraits>(
                                  index, destination, size, handled),
                              0))...});
    if (handled)
      return result;
    return fallback_.read_raw(index, destination, size);
  }

  [[nodiscard]] bool write(uint32_t index, const void *source,
                           size_t size) noexcept {
    bool handled = false;
    bool result = false;
    using expand = int[];
    static_cast<void>(
        expand{0, (handled ? 0
                           : (result = try_write<FieldTraits>(
                                  index, source, size, handled),
                              0))...});
    if (handled)
      return result;
    return fallback_.write_raw(index, source, size);
  }

  static bool read_thunk(const void *opaque, address_space_ref, uint32_t index,
                         void *destination, size_t size) noexcept {
    return static_cast<const register_context_ref_with *>(opaque)->read(
        index, destination, size);
  }

  static bool write_thunk(void *opaque, address_space_ref, uint32_t index,
                          const void *source, size_t size) noexcept {
    return static_cast<register_context_ref_with *>(opaque)->write(
        index, source, size);
  }

  inline static constexpr register_context_vtable vtable_{read_thunk,
                                                           write_thunk};

  State *state_;
  bool writable_;
  address_space_ref space_;
  span<std::byte> scratch_;
  register_context_ref fallback_;
};

} // namespace microfmt