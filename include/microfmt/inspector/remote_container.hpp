// ============================================================================
// General Type-Erased Remote Container View & Context Builder Pattern
// ============================================================================

#pragma once

#include "address_space.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

/**
 * @brief Configuration options for formatting container views.
 */
struct container_options {
  bool print_key{true};
  bool print_value{true};
  std::string_view kv_separator{": "};
  std::string_view entry_separator{", "};
  std::string_view open_bracket{"{"};
  std::string_view close_bracket{"}"};
  size_t max_print{64};
};

/**
 * @brief External context holder capturing container state and
 * traversal/formatting logic.
 *
 * Created via make_context(...) and stored safely in the caller's stack frame.
 */
template <typename IteratorState, typename FormatterFn>
class container_context {
public:
  constexpr container_context(IteratorState initial_state,
                              FormatterFn formatter)
      : state_(initial_state), formatter_fn_(formatter) {}

  // Type-erased execution thunk receiving options managed by the view
  static bool format_thunk(void *raw_ctx, address_space_ref space,
                           span<std::byte> scratch, const sink &out,
                           const container_options &opts) noexcept {
    auto *self = static_cast<container_context *>(raw_ctx);
    return self->formatter_fn_(self->state_, opts, space, scratch, out);
  }

  [[nodiscard]] constexpr IteratorState &state() noexcept { return state_; }
  [[nodiscard]] constexpr const IteratorState &state() const noexcept {
    return state_;
  }

private:
  IteratorState state_;
  FormatterFn formatter_fn_;
};

/**
 * @brief Factory helper to build an external container context.
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
class remote_container_view {
public:
  using format_fn_t = bool (*)(void *ctx, address_space_ref space,
                               span<std::byte> scratch, const sink &out,
                               const container_options &opts) noexcept;

  constexpr remote_container_view() noexcept = default;

  /**
   * @brief Constructs a remote_container_view with optional explicit
   * container_options.
   */
  template <typename Context>
  constexpr remote_container_view(uintptr_t container_addr,
                                  address_space_ref space,
                                  span<std::byte> scratch, Context *ctx,
                                  container_options options = {}) noexcept
      : container_addr_(container_addr), space_(space), scratch_(scratch),
        ctx_(ctx), format_fn_(ctx ? &Context::format_thunk : nullptr),
        options_(options) {}

  [[nodiscard]] constexpr uintptr_t container_address() const noexcept {
    return container_addr_;
  }
  [[nodiscard]] constexpr address_space_ref space() const noexcept {
    return space_;
  }
  [[nodiscard]] constexpr span<std::byte> scratch() const noexcept {
    return scratch_;
  }
  [[nodiscard]] constexpr const container_options &options() const noexcept {
    return options_;
  }
  [[nodiscard]] constexpr bool is_null() const noexcept {
    return container_addr_ == 0 || !ctx_;
  }

  bool format(const sink &out) const noexcept {
    if (!format_fn_ || !ctx_)
      return false;
    return format_fn_(ctx_, space_, scratch_, out, options_);
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return !is_null();
  }

private:
  uintptr_t container_addr_{0};
  address_space_ref space_{};
  span<std::byte> scratch_{};
  void *ctx_{nullptr};
  format_fn_t format_fn_{nullptr};
  container_options options_;
};

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
