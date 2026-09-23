// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// ============================================================================
// General Type-Erased Remote Container View & Context Builder Pattern
// ============================================================================

#pragma once

/** @file remote_container.hpp
 * @brief Type-erased views and contexts for formatting remote containers. */

#include "address_space.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

class remote_container_view;

/**
 * @brief Configuration options for formatting container views.
 */
struct MICROFMT_API_CLASS container_options {
  /// Whether key components of entries are rendered.
  bool print_key{true};
  /// Whether value components of entries are rendered.
  bool print_value{true};
  /// Text separating a key from its value.
  microfmt::string_view kv_separator{": "};
  /// Text separating adjacent entries.
  microfmt::string_view entry_separator{", "};
  /// Opening delimiter for the rendered container.
  microfmt::string_view open_bracket{"{"};
  /// Closing delimiter for the rendered container.
  microfmt::string_view close_bracket{"}"};
  /// Maximum number of entries to render before truncating.
  size_t max_print{64};
};

/**
 * @brief External context holder capturing container state and
 * traversal/formatting logic.
 *
 * Created via @ref make_container_context and stored safely in the caller's
 * stack frame.
 *
 * @tparam IteratorState State retained while traversing the container.
 * @tparam FormatterFn Callable used to format the container.
 */
template <typename IteratorState, typename FormatterFn>
class RELOCO_OWNER container_context {
public:
  /**
   * @brief Stores container traversal state and its formatting callback.
   * @param initial_state Initial state used by @p formatter.
   * @param formatter Callable that renders the container.
   */
  constexpr container_context(IteratorState initial_state,
                              FormatterFn formatter)
      : state_(initial_state), formatter_fn_(formatter) {}

  /**
   * @brief Invokes the stored formatter through the type-erased view API.
   * @param raw_ctx Pointer to this context.
   * @param space Address space containing the container.
   * @param scratch Reusable scratch storage for remote reads.
   * @param out Output destination.
   * @param opts Rendering options selected by the view.
   * @return The result reported by the stored formatter.
   */
  static bool format_thunk(void *raw_ctx, address_space_ref space,
                           span<std::byte> scratch, const sink &out,
                           const container_options &opts) noexcept {
    auto *self = static_cast<container_context *>(raw_ctx);
    return self->formatter_fn_(self->state_, opts, space, scratch, out);
  }

  /**
   * @brief Returns the mutable traversal state.
   * @return Reference to the stored state.
   */
  [[nodiscard]] constexpr IteratorState &state() noexcept { return state_; }
  /**
   * @brief Returns the traversal state.
   * @return Const reference to the stored state.
   */
  [[nodiscard]] constexpr const IteratorState &state() const noexcept {
    return state_;
  }

private:
  IteratorState state_;
  FormatterFn formatter_fn_;
};

/**
 * @brief Owning typed container bound to a compile-time traits dispatcher.
 *
 * The context is retained by value. Traversal operations remain compile-time
 * trait calls; only conversion to @ref remote_container_view introduces type
 * erasure.
 */
template <typename Tag, typename Dispatcher>
class RELOCO_OWNER basic_remote_container {
public:
  using traits_type = typename Dispatcher::traits_type;
  using context_type = typename traits_type::context_type;

  constexpr basic_remote_container(uintptr_t container_addr,
                                   context_type context) noexcept
      : container_addr_(container_addr), context_(std::move(context)) {}

  [[nodiscard]] constexpr uintptr_t container_address() const noexcept {
    return container_addr_;
  }

  [[nodiscard]] constexpr value_ref<context_type>
  context() & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<context_type>(context_);
  }

  [[nodiscard]] constexpr value_ref<const context_type>
  context() const & noexcept RELOCO_LIFETIMEBOUND {
    return value_ref<const context_type>(context_);
  }

  value_ref<context_type> context() && = delete;
  value_ref<const context_type> context() const && = delete;

  [[nodiscard]] constexpr remote_container_view
  view(address_space_ref space,
       span<std::byte> scratch RELOCO_LIFETIMEBOUND,
       container_options options = {}) & noexcept RELOCO_LIFETIMEBOUND;

  remote_container_view view(address_space_ref, span<std::byte>,
                             container_options = {}) && = delete;

  static bool format_thunk(void *raw_ctx, address_space_ref space,
                           span<std::byte> scratch, const sink &out,
                           const container_options &opts) noexcept {
    auto &self = *static_cast<basic_remote_container *>(raw_ctx);
    return Dispatcher::format(value_ref<const context_type>(self.context_),
                              self.container_addr_, opts, space, scratch, out);
  }

private:
  uintptr_t container_addr_;
  context_type context_;
};

/**
 * @brief Creates an external context for a remote container view.
 * @tparam IteratorState State retained while traversing the container.
 * @tparam FormatterFn Callable type used to render the container.
 * @param initial_state Initial state supplied to @p formatter.
 * @param formatter Callable accepting state, options, address space, scratch
 * storage, and sink.
 * @return A @ref container_context retaining the supplied state and callable.
 */
template <typename IteratorState, typename FormatterFn>
[[nodiscard]] constexpr auto
make_container_context(IteratorState initial_state,
                       FormatterFn formatter) noexcept {
  return container_context<IteratorState, FormatterFn>(initial_state,
                                                       formatter);
}

/**
 * @brief General type-erased view over any remote container or data structure.
 *        Manages its own formatting options directly.
 */
class MICROFMT_API_CLASS RELOCO_POINTER remote_container_view {
public:
  /**
   * @brief Type-erased function signature used to format a container.
   */
  using format_fn_t = bool (*)(void *ctx, address_space_ref space,
                               span<std::byte> scratch, const sink &out,
                               const container_options &opts) noexcept;

  /**
   * @brief Constructs an empty, invalid view.
   */
  constexpr remote_container_view() noexcept = default;

  /**
   * @brief Constructs a remote-container view with explicit options.
   * @tparam Context Concrete @ref container_context type.
   * @param container_addr Address of the remote container.
   * @param space Address space containing the container.
   * @param scratch Reusable scratch storage for traversal.
   * @param ctx Required context whose lifetime must exceed this view.
   * @param options Formatting configuration.
   */
  template <typename Context>
  constexpr remote_container_view(uintptr_t container_addr,
                                  address_space_ref space,
                                  span<std::byte> scratch
                                      RELOCO_LIFETIMEBOUND
                                          RELOCO_LIFETIME_CAPTURE_BY_THIS,
                                  value_ref<Context> ctx
                                      RELOCO_LIFETIME_CAPTURE_BY_THIS,
                                  container_options options = {}) noexcept
      : container_addr_(container_addr), space_(space), scratch_(scratch),
        ctx_(ctx.pointer()), format_fn_(&Context::format_thunk),
        options_(options) {}

  /**
   * @brief Returns the remote container address.
   * @return Absolute container address.
   */
  [[nodiscard]] constexpr uintptr_t container_address() const noexcept {
    return container_addr_;
  }
  /**
   * @brief Returns the address space.
   * @return Address-space handle used for reads.
   */
  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  /**
   * @brief Returns the reusable scratch storage.
   * @return Scratch byte span.
   */
  [[nodiscard]] constexpr span<std::byte>
  scratch() const noexcept RELOCO_LIFETIMEBOUND {
    return scratch_;
  }
  /**
   * @brief Returns the rendering configuration.
   * @return Const reference to the view options.
   */
  [[nodiscard]] constexpr const container_options &
  options() const & noexcept RELOCO_LIFETIMEBOUND {
    return options_;
  }
  const container_options &options() const && = delete;
  /**
   * @brief Reports whether the view lacks a container address or context.
   * @return `true` when the view cannot be formatted.
   */
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return container_addr_ == 0 || !ctx_;
  }

  /**
   * @brief Formats the remote container through its bound context.
   * @param out Destination sink.
   * @return `true` when the formatter succeeds; `false` for an invalid view or
   * formatter failure.
   */
  bool format(const sink &out) const noexcept {
    if (!format_fn_ || !ctx_)
      return false;
    return format_fn_(ctx_.get(), space_, scratch_, out, options_);
  }

  /**
   * @brief Reports whether the view can be formatted.
   * @return `true` when the view is non-null.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !is_null();
  }

private:
  uintptr_t container_addr_{0};
  address_space_ref space_{};
  span<std::byte> scratch_{};
  value_ptr<void> ctx_{};
  format_fn_t format_fn_{nullptr};
  container_options options_;
};

template <typename Tag, typename Dispatcher>
[[nodiscard]] constexpr remote_container_view
basic_remote_container<Tag, Dispatcher>::view(
    address_space_ref space, span<std::byte> scratch,
    container_options options) & noexcept {
  return remote_container_view(container_addr_, space, scratch,
                               value_ref<basic_remote_container>(*this),
                               options);
}

} // namespace microfmt

// ============================================================================
// Formatter for remote_container_view
// ============================================================================

template <> struct microfmt::formatter<microfmt::remote_container_view> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}

  void format(const microfmt::remote_container_view &view,
              const microfmt::sink &out) const noexcept {
    if (view.is_null()) {
      out.write("<null>");
      return;
    }
    if (!view.format(out)) {
      out.write("<fault>");
    }
  }
};
