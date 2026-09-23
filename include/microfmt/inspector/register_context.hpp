// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "address_space.hpp"
#include "dwarf_registers.hpp"
#include "../microfmt.hpp"
#include "../reloco.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace microfmt {

/**
 * @brief Static customization point for a register-context provider.
 *
 * A specialization declares `context_type` and may provide `read_register`
 * and/or `write_register`. Read operations receive
 * `value_ref<const context_type>`; writes receive `value_ref<context_type>`.
 */
template <typename Tag> struct register_context_traits;

template <typename State, auto Read>
struct read_only_register_context_tag {};

template <typename State, auto Write>
struct write_only_register_context_tag {};

template <typename State, auto Read, auto Write>
struct read_write_register_context_tag {};

template <typename State> struct empty_register_context_tag {};

template <typename State>
struct register_context_traits<empty_register_context_tag<State>> {
  using context_type = State;
};

template <typename State, auto Read>
struct register_context_traits<read_only_register_context_tag<State, Read>> {
  using context_type = State;

  static bool read_register(value_ref<const context_type> context,
                            address_space_ref space, uint32_t index,
                            void *destination, size_t size) noexcept {
    return Read(context.get(), space, index, destination, size);
  }
};

template <typename State, auto Write>
struct register_context_traits<write_only_register_context_tag<State, Write>> {
  using context_type = State;

  static bool write_register(value_ref<context_type> context,
                             address_space_ref space, uint32_t index,
                             const void *source, size_t size) noexcept {
    return Write(context.get(), space, index, source, size);
  }
};

template <typename State, auto Read, auto Write>
struct register_context_traits<
    read_write_register_context_tag<State, Read, Write>> {
  using context_type = State;

  static bool read_register(value_ref<const context_type> context,
                            address_space_ref space, uint32_t index,
                            void *destination, size_t size) noexcept {
    return Read(context.get(), space, index, destination, size);
  }

  static bool write_register(value_ref<context_type> context,
                             address_space_ref space, uint32_t index,
                             const void *source, size_t size) noexcept {
    return Write(context.get(), space, index, source, size);
  }
};

namespace detail {

template <typename Tag, typename = void>
struct has_register_context_read : std::false_type {};

template <typename Tag>
struct has_register_context_read<
    Tag, std::void_t<decltype(register_context_traits<Tag>::read_register(
             std::declval<
                 value_ref<const typename register_context_traits<Tag>::
                               context_type>>(),
             std::declval<address_space_ref>(), std::declval<uint32_t>(),
             std::declval<void *>(), std::declval<size_t>()))>>
    : std::true_type {};

template <typename Tag, typename = void>
struct has_register_context_write : std::false_type {};

template <typename Tag>
struct has_register_context_write<
    Tag, std::void_t<decltype(register_context_traits<Tag>::write_register(
             std::declval<
                 value_ref<typename register_context_traits<Tag>::context_type>>(),
             std::declval<address_space_ref>(), std::declval<uint32_t>(),
             std::declval<const void *>(), std::declval<size_t>()))>>
    : std::true_type {};

template <typename State, typename... FieldTraits>
struct mapped_register_context_tag {};

} // namespace detail

/**
 * @brief Zero-allocation, type-erased handle for inspecting and modifying CPU
 * registers from a frame context.
 */
class MICROFMT_API_CLASS RELOCO_POINTER register_context_ref {
public:
  constexpr register_context_ref() noexcept = default;

  template <typename Tag, typename State,
            typename Traits = register_context_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 State *, typename Traits::context_type *>,
                             int> = 0>
  constexpr register_context_ref(
      Tag, State &state RELOCO_LIFETIMEBOUND
               RELOCO_LIFETIME_CAPTURE_BY_THIS,
      address_space_ref space,
      span<std::byte> scratch RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : state_(&state), mutable_state_(&state), vtable_(&s_vtable<Tag>),
        space_(space), scratch_(scratch) {}

  template <typename Tag, typename State,
            typename Traits = register_context_traits<Tag>,
            std::enable_if_t<std::is_convertible_v<
                                 const State *,
                                 const typename Traits::context_type *>,
                             int> = 0>
  constexpr register_context_ref(
      Tag, const State &state RELOCO_LIFETIMEBOUND
               RELOCO_LIFETIME_CAPTURE_BY_THIS,
      address_space_ref space,
      span<std::byte> scratch RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : state_(&state), vtable_(&s_vtable<Tag>), space_(space),
        scratch_(scratch) {}

  template <typename Tag, typename State,
            std::enable_if_t<!std::is_lvalue_reference_v<State>, int> = 0>
  constexpr register_context_ref(Tag, State &&, address_space_ref,
                                 span<std::byte>) = delete;

  [[nodiscard]] constexpr bool is_null() const noexcept {
    return !state_ || !vtable_ ||
           (!vtable_->read_register && !vtable_->write_register);
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
    if (!vtable_ || !vtable_->read_register)
      return false;
    return vtable_->read_register(state_.get(), space_, dwarf_reg_index,
                                  &out_value, sizeof(T));
  }

  /**
   * @brief Reads raw register bytes into a buffer.
   */
  [[nodiscard]] bool read_raw(uint32_t dwarf_reg_index, void *dest,
                              size_t size) const noexcept {
    if (!vtable_ || !vtable_->read_register)
      return false;
    return vtable_->read_register(state_.get(), space_, dwarf_reg_index, dest,
                                  size);
  }

  /**
   * @brief Writes a typed value into a register by its DWARF index.
   */
  template <typename T>
  [[nodiscard]] bool write(uint32_t dwarf_reg_index,
                           const T &value) const noexcept {
    if (!vtable_ || !vtable_->write_register)
      return false;
    if (!mutable_state_)
      return false;
    return vtable_->write_register(mutable_state_.get(), space_,
                                   dwarf_reg_index, &value, sizeof(T));
  }

  /**
   * @brief Writes raw bytes from a source buffer into a register.
   */
  [[nodiscard]] bool write_raw(uint32_t dwarf_reg_index, const void *src,
                               size_t size) const noexcept {
    if (!vtable_ || !vtable_->write_register)
      return false;
    if (!mutable_state_)
      return false;
    return vtable_->write_register(mutable_state_.get(), space_,
                                   dwarf_reg_index, src, size);
  }

  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr span<std::byte>
  scratch() const noexcept RELOCO_LIFETIMEBOUND {
    return scratch_;
  }

private:
  struct vtable {
    bool (*read_register)(const void *, address_space_ref, uint32_t, void *,
                          size_t) noexcept;
    bool (*write_register)(void *, address_space_ref, uint32_t, const void *,
                           size_t) noexcept;
  };

  template <typename Tag>
  [[nodiscard]] static constexpr auto read_entry() noexcept {
    if constexpr (detail::has_register_context_read<Tag>::value) {
      return +[](const void *state, address_space_ref space, uint32_t index,
                 void *destination, size_t size) noexcept {
        using context_type = typename register_context_traits<Tag>::context_type;
        const auto &context = *static_cast<const context_type *>(state);
        return register_context_traits<Tag>::read_register(
            value_ref<const context_type>(context), space, index, destination,
            size);
      };
    } else {
      return static_cast<bool (*)(const void *, address_space_ref, uint32_t,
                                  void *, size_t) noexcept>(nullptr);
    }
  }

  template <typename Tag>
  [[nodiscard]] static constexpr auto write_entry() noexcept {
    if constexpr (detail::has_register_context_write<Tag>::value) {
      return +[](void *state, address_space_ref space, uint32_t index,
                 const void *source, size_t size) noexcept {
        using context_type = typename register_context_traits<Tag>::context_type;
        auto &context = *static_cast<context_type *>(state);
        return register_context_traits<Tag>::write_register(
            value_ref<context_type>(context), space, index, source, size);
      };
    } else {
      return static_cast<bool (*)(void *, address_space_ref, uint32_t,
                                  const void *, size_t) noexcept>(nullptr);
    }
  }

  template <typename Tag>
  static constexpr vtable s_vtable{read_entry<Tag>(), write_entry<Tag>()};

  value_ptr<const void> state_{};
  value_ptr<void> mutable_state_{};
  const vtable *vtable_{nullptr};
  address_space_ref space_{};
  span<std::byte> scratch_{};
};

template <auto Read, typename State>
[[nodiscard]] constexpr register_context_ref
make_read_only_register_context_ref(
    State &state RELOCO_LIFETIMEBOUND, address_space_ref space,
    span<std::byte> scratch RELOCO_LIFETIMEBOUND) noexcept {
  using state_type = std::remove_const_t<State>;
  using tag = read_only_register_context_tag<state_type, Read>;
  return register_context_ref(tag{}, state, space, scratch);
}

template <auto Read, auto Write, typename State>
[[nodiscard]] constexpr register_context_ref
make_register_context_ref(
    State &state RELOCO_LIFETIMEBOUND, address_space_ref space,
    span<std::byte> scratch RELOCO_LIFETIMEBOUND) noexcept {
  using state_type = std::remove_const_t<State>;
  using tag = read_write_register_context_tag<state_type, Read, Write>;
  return register_context_ref(tag{}, state, space, scratch);
}

/**
 * @brief Typed owner for a register-context traits specialization.
 */
template <typename Tag> class RELOCO_OWNER register_context {
public:
  using traits_type = register_context_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr register_context(
      context_type context, address_space_ref space,
      span<std::byte> scratch RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : context_(std::move(context)), space_(space), scratch_(scratch) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr register_context_ref
  ref() & noexcept RELOCO_LIFETIMEBOUND {
    return register_context_ref(Tag{}, context_, space_, scratch_);
  }

  [[nodiscard]] constexpr register_context_ref
  ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return register_context_ref(Tag{}, context_, space_, scratch_);
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  register_context_ref ref() && = delete;
  register_context_ref ref() const && = delete;

private:
  context_type context_;
  address_space_ref space_;
  span<std::byte> scratch_;
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
class RELOCO_POINTER register_context_ref_with {
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
      State &state RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS,
      address_space_ref space,
      span<std::byte> scratch RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS,
      register_context_ref fallback = {}) noexcept
      : state_(state), mutable_state_(&state), space_(space),
        scratch_(scratch), fallback_(fallback) {}

  constexpr register_context_ref_with(
      const State &state RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS,
      address_space_ref space,
      span<std::byte> scratch RELOCO_LIFETIMEBOUND
          RELOCO_LIFETIME_CAPTURE_BY_THIS,
      register_context_ref fallback = {}) noexcept
      : state_(state), space_(space), scratch_(scratch),
        fallback_(fallback) {}

  register_context_ref_with(State &&, address_space_ref, span<std::byte>,
                            register_context_ref = {}) = delete;
  register_context_ref_with(const State &&, address_space_ref,
                            span<std::byte>,
                            register_context_ref = {}) = delete;

  /**
   * @brief Creates a type-erased reference borrowing this wrapper.
   *
   * This wrapper, its state, scratch storage, address-space context, and any
   * fallback context must outlive the returned reference.
   */
  [[nodiscard]] constexpr register_context_ref
  ref() & noexcept RELOCO_LIFETIMEBOUND {
    using tag = detail::mapped_register_context_tag<State, FieldTraits...>;
    return register_context_ref(tag{}, *this, space_, scratch_);
  }

  [[nodiscard]] constexpr operator register_context_ref() & noexcept
      RELOCO_LIFETIMEBOUND {
    return ref();
  }

  register_context_ref ref() && = delete;
  operator register_context_ref() && = delete;

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
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(destination, &value, sizeof(value));
      RELOCO_END_UNSAFE_BUFFER_USAGE;
      return true;
    } else {
      const auto *value = Field::get(*state_, index);
      if (!value)
        return false;
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(destination, value, sizeof(*value));
      RELOCO_END_UNSAFE_BUFFER_USAGE;
      return true;
    }
  }

  template <typename Field>
  [[nodiscard]] bool try_write(uint32_t index, const void *source, size_t size,
                               bool &handled) noexcept {
    if (handled || !Field::matches(index))
      return false;

    handled = true;
    if (!mutable_state_ || !source ||
        size != sizeof(typename Field::value_type))
      return false;

    if constexpr (detail::has_register_field_write<Field, State>::value) {
      typename Field::value_type value{};
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(&value, source, sizeof(value));
      RELOCO_END_UNSAFE_BUFFER_USAGE;
      return Field::write(*mutable_state_, space_, index, value);
    } else if constexpr (detail::has_mutable_register_field_get<Field,
                                                                 State>::value) {
      auto *value = Field::get(*mutable_state_, index);
      if (!value)
        return false;
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::memcpy(value, source, sizeof(*value));
      RELOCO_END_UNSAFE_BUFFER_USAGE;
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

  friend struct register_context_traits<
      detail::mapped_register_context_tag<State, FieldTraits...>>;

  value_ref<const State> state_;
  value_ptr<State> mutable_state_;
  address_space_ref space_;
  span<std::byte> scratch_;
  register_context_ref fallback_;
};

template <typename State, typename... FieldTraits>
struct register_context_traits<
    detail::mapped_register_context_tag<State, FieldTraits...>> {
  using context_type = register_context_ref_with<State, FieldTraits...>;

  static bool read_register(value_ref<const context_type> context,
                            address_space_ref, uint32_t index,
                            void *destination, size_t size) noexcept {
    return context->read(index, destination, size);
  }

  static bool write_register(value_ref<context_type> context,
                             address_space_ref, uint32_t index,
                             const void *source, size_t size) noexcept {
    return context->write(index, source, size);
  }
};

} // namespace microfmt