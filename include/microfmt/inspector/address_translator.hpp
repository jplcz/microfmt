// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file address_translator.hpp
 * @brief Type-erased virtual-to-physical address translator handle with
 *        protection attributes and target memory space identifiers. */

#include "address_space.hpp"
#include <cstdint>
#include <type_traits>

namespace microfmt {

/**
 * @brief Attributes and physical mapping details resolved for a virtual
 * address.
 */
struct MICROFMT_API_CLASS translation_attributes {
  /// Translated physical address.
  uintptr_t physical_address{0};

  /// Target memory space identifier (e.g., ASID, VM space ID, or domain tag).
  uint8_t space_id{0};

  /// Security state indicator (e.g., true = Secure [S], false = Non-Secure [NS]
  /// on ARM TrustZone).
  bool is_secure{false};

  /// Page permits read access.
  bool readable{false};

  /// Page permits write access.
  bool writable{false};

  /// Page permits instruction execution.
  bool executable{false};

  /// Page permits unprivileged (user-mode) access.
  bool user_accessible{false};

  /**
   * @brief Reports whether the attribute record represents a valid translation.
   */
  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return readable || writable || executable;
  }
};

/**
 * @brief Static customization point describing a virtual-to-physical translator
 * backend.
 * @tparam Tag Tag identifying the translator implementation.
 */
template <typename Tag> struct address_translator_traits;

/**
 * @brief Type-erased, two-word handle for virtual-to-physical address
 * translation.
 *
 * Packs a context pointer and a virtual table into two words, avoiding
 * allocations, RTTI, and virtual dispatch.
 */
class RELOCO_POINTER address_translator_ref {
public:
  /**
   * @brief Virtual table of address translation operations.
   */
  struct vtable {
    /**
     * @brief Translates a virtual address and retrieves its protection and
     * space attributes.
     * @param ctx Opaque pointer to the concrete context state.
     * @param virt_addr Virtual address to translate.
     * @param out_attrs Receives the resolved physical address and memory
     * attributes on success.
     * @return `true` if translation succeeded and the page is present, `false`
     * otherwise.
     */
    bool (*translate)(const void *ctx, uintptr_t virt_addr,
                      translation_attributes &out_attrs) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr address_translator_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless translator tag.
   */
  template <
      typename Tag, typename Traits = address_translator_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit address_translator_ref(Tag) noexcept
      : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful translator tag.
   */
  template <typename Tag, typename Context,
            typename Traits = address_translator_traits<Tag>,
            std::enable_if_t<
                !std::is_void_v<typename Traits::context_type> &&
                    std::is_convertible_v<
                        const Context *, const typename Traits::context_type *>,
                int> = 0>
  constexpr address_translator_ref(
      Tag, const Context &ctx RELOCO_LIFETIMEBOUND
               RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr address_translator_ref(Tag, Context &&) = delete;

  template <
      typename Tag, typename Traits = address_translator_traits<Tag>,
      std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_translator_ref make() noexcept {
    return address_translator_ref(Tag{});
  }

  template <
      typename Tag, typename Context,
      typename Traits = address_translator_traits<Tag>,
      std::enable_if_t<!std::is_void_v<typename Traits::context_type>, int> = 0>
  [[nodiscard]] static constexpr address_translator_ref
  make(const Context &ctx RELOCO_LIFETIMEBOUND) noexcept {
    return address_translator_ref(Tag{}, ctx);
  }

  template <typename Tag, typename Context,
            std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  static address_translator_ref make(Context &&) = delete;

  /**
   * @brief Translates a virtual address to physical address and populates
   * attributes.
   * @param virt_addr Virtual address to translate.
   * @param out_attrs Receives translation attributes.
   * @return `true` on success, `false` if the handle is empty or translation
   * fails.
   */
  [[nodiscard]] bool
  translate(uintptr_t virt_addr,
            translation_attributes &out_attrs) const noexcept {
    if (!vtbl_ || !vtbl_->translate)
      return false;
    return vtbl_->translate(ctx_.get(), virt_addr, out_attrs);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return vtbl_ != nullptr;
  }

private:
  template <typename Tag>
  static bool translate_entry(const void *context, uintptr_t address,
                              translation_attributes &attributes) noexcept {
    using context_type = typename address_translator_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      return address_translator_traits<Tag>::translate(address, attributes);
    } else {
      const auto &typed_context =
          *static_cast<const context_type *>(context);
      return address_translator_traits<Tag>::translate(
          value_ref<const context_type>(typed_context), address, attributes);
    }
  }

  template <typename Tag>
  static constexpr vtable s_vtbl{&translate_entry<Tag>};

  value_ptr<const void> ctx_{};
  const vtable *vtbl_{nullptr};
};

template <typename Tag,
          bool Stateless = std::is_void_v<
              typename address_translator_traits<Tag>::context_type>>
class address_translator;

template <typename Tag>
class RELOCO_OWNER address_translator<Tag, false> {
public:
  using traits_type = address_translator_traits<Tag>;
  using context_type = typename traits_type::context_type;

  constexpr explicit address_translator(context_type context) noexcept
      : context_(std::move(context)) {}

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  [[nodiscard]] constexpr address_translator_ref
  ref() const & noexcept RELOCO_LIFETIMEBOUND {
    return address_translator_ref(Tag{}, context_);
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;
  address_translator_ref ref() const && = delete;

private:
  context_type context_;
};

template <typename Tag> class address_translator<Tag, true> {
public:
  [[nodiscard]] static constexpr address_translator_ref ref() noexcept {
    return address_translator_ref(Tag{});
  }
};

} // namespace microfmt