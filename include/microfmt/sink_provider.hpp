// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file sink_provider.hpp
 * @brief Traits-based, type-erased sink provider customization point.
 *
 * Mirrors reloco's `context_type`-based provider pattern (see reloco's
 * `docs/extending.md`, "Extending reloco: the context_type-based provider
 * pattern", which is itself adapted from microfmt): an empty tag selects a
 * `sink_provider_traits<Tag>` specialization declaring a `context_type` and
 * static operations; a typed `sink_provider<Tag>` wrapper or caller-owned
 * context object holds that state by value; and the type-erased
 * `sink_provider_ref` borrows it for use in non-templated code. No virtual
 * interfaces, no allocation, and no runtime registration are involved.
 *
 * This is an additive customization point alongside the hand-written
 * `as_sink()` methods already on `span_sink`, `buffer_sink`, and friends
 * (see microfmt.hpp) -- it exists for backends that want the same
 * boilerplate reloco's `allocator_ref` gets (mandatory/optional operation
 * detection, stateless-vs-stateful tag dispatch) instead of writing a
 * context pointer + lambda pair by hand. Both shapes ultimately produce an
 * ordinary `microfmt::sink`, so either can be passed to `format_to`/
 * `vformat_to` interchangeably.
 */

#include "microfmt.hpp"

#include <type_traits>
#include <utility>

namespace microfmt {

/**
 * @brief Static customization point describing a sink backend.
 *
 * Specialize for a tag type to provide `context_type` and `write`. Add
 * `flush` to expose that optional operation; omitting it makes it report as
 * unsupported through @ref sink_provider_ref::can_flush instead of failing
 * at compile time.
 *
 * For a stateless backend (answers purely from global/static state, e.g. a
 * discard/null sink), declare `using context_type = void;` and drop the
 * `value_ref<context_type>` parameter from every operation.
 *
 * @tparam Tag Tag identifying the sink implementation.
 */
template <typename Tag> struct sink_provider_traits;

namespace detail {

template <typename Tag, typename = void> struct has_sink_provider_flush : std::false_type {};

template <typename Tag>
struct has_sink_provider_flush<Tag, std::void_t<decltype(sink_provider_traits<Tag>::flush(
                                        std::declval<value_ref<typename sink_provider_traits<Tag>::context_type>>()))>>
    : std::true_type {};

template <typename Tag, typename = void> struct has_stateless_sink_provider_flush : std::false_type {};

template <typename Tag>
struct has_stateless_sink_provider_flush<Tag, std::void_t<decltype(sink_provider_traits<Tag>::flush())>>
    : std::true_type {};

} // namespace detail

/**
 * @brief Type-erased, two-word handle to a sink backend.
 *
 * Packs a context pointer and a vtable pointer into two words: no virtual
 * base class, no RTTI, and no allocation of its own. The bound context (for
 * stateful tags) must outlive every `sink_provider_ref` built from it.
 * Converts to @ref microfmt::sink via @ref as_sink so it plugs directly into
 * `format_to`/`vformat_to`.
 */
class MICROFMT_API_CLASS RELOCO_POINTER sink_provider_ref {
public:
  /**
   * @brief Virtual table of sink operations.
   */
  struct vtable {
    /**
     * @brief Writes a character slice. See @ref sink_provider_ref::write.
     *
     * Shares its signature with @ref microfmt::sink::write_fn_t by design,
     * so @ref as_sink can hand this function pointer straight to a `sink`
     * with no adapter.
     */
    void (*write)(void *ctx, microfmt::string_view sv) noexcept;
    /**
     * @brief Flushes buffered output. `nullptr` for backends whose traits
     * omit `flush`. See @ref sink_provider_ref::flush.
     */
    void (*flush)(void *ctx) noexcept;
  };

  /**
   * @brief Constructs an empty (invalid) handle.
   */
  constexpr sink_provider_ref() noexcept = default;

  /**
   * @brief Constructs a handle for a stateless sink tag.
   * @tparam Tag Sink tag type.
   * @tparam Traits Specialized traits, enabled when `context_type` is `void`.
   */
  template <typename Tag, typename Traits = sink_provider_traits<Tag>,
            std::enable_if_t<std::is_void_v<typename Traits::context_type>, int> = 0>
  constexpr explicit sink_provider_ref(Tag) noexcept : ctx_(nullptr), vtbl_(&s_vtbl<Tag>) {}

  /**
   * @brief Constructs a handle for a stateful sink tag.
   * @tparam Tag Sink tag type.
   * @tparam Context Concrete context type.
   * @tparam Traits Specialized traits, enabled when `context_type` is
   * non-void and @p Context converts to it.
   * @param ctx Context object backing the writes.
   */
  template <typename Tag, typename Context, typename Traits = sink_provider_traits<Tag>,
            std::enable_if_t<!std::is_void_v<typename Traits::context_type> &&
                                 std::is_convertible_v<Context *, typename Traits::context_type *>,
                             int> = 0>
  constexpr sink_provider_ref(Tag, Context &ctx RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ctx_(&ctx), vtbl_(&s_vtbl<Tag>) {}

  // Refuses to bind to a temporary context, which would leave ctx_ dangling
  // the moment this constructor returns.
  template <typename Tag, typename Context, std::enable_if_t<!std::is_lvalue_reference_v<Context>, int> = 0>
  constexpr sink_provider_ref(Tag, Context &&) = delete;

  /**
   * @brief Emits a sequence of characters to the underlying sink.
   *
   * If @p sv is empty or the handle is empty, this operation is a no-op.
   * @param sv String view containing characters to write.
   */
  void write(microfmt::string_view sv) const noexcept {
    if (vtbl_ && !sv.empty()) {
      vtbl_->write(ctx_.get(), sv);
    }
  }

  /**
   * @brief Reports whether the bound backend supports `flush`.
   */
  [[nodiscard]] constexpr bool can_flush() const noexcept { return vtbl_ && vtbl_->flush; }

  /**
   * @brief Flushes buffered output. A no-op when @ref can_flush is `false`.
   */
  void flush() const noexcept {
    if (can_flush()) {
      vtbl_->flush(ctx_.get());
    }
  }

  /**
   * @brief Reports whether the handle is bound to a sink backend.
   */
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return vtbl_ != nullptr; }

  /**
   * @brief Bridges to a plain @ref microfmt::sink, for use with
   * `format_to`/`vformat_to`.
   *
   * Valid only as long as this handle (and the context it may be bound to)
   * stays alive; the returned `sink` borrows the same context pointer.
   */
  [[nodiscard]] constexpr sink as_sink() const noexcept RELOCO_LIFETIMEBOUND {
    return sink{ctx_.get(), vtbl_ ? vtbl_->write : nullptr};
  }

private:
  template <typename Tag> static void write_entry(void *ctx, microfmt::string_view sv) noexcept {
    using context_type = typename sink_provider_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      (void)ctx;
      sink_provider_traits<Tag>::write(sv);
    } else {
      auto &typed = *static_cast<context_type *>(ctx);
      sink_provider_traits<Tag>::write(value_ref<context_type>(typed), sv);
    }
  }

  template <typename Tag> [[nodiscard]] static constexpr auto flush_entry() noexcept {
    using context_type = typename sink_provider_traits<Tag>::context_type;
    if constexpr (std::is_void_v<context_type>) {
      if constexpr (detail::has_stateless_sink_provider_flush<Tag>::value) {
        return +[](void *) noexcept { sink_provider_traits<Tag>::flush(); };
      } else {
        return static_cast<void (*)(void *) noexcept>(nullptr);
      }
    } else if constexpr (detail::has_sink_provider_flush<Tag>::value) {
      return +[](void *ctx) noexcept {
        auto &typed = *static_cast<context_type *>(ctx);
        sink_provider_traits<Tag>::flush(value_ref<context_type>(typed));
      };
    } else {
      return static_cast<void (*)(void *) noexcept>(nullptr);
    }
  }

  template <typename Tag> static constexpr vtable s_vtbl{&write_entry<Tag>, flush_entry<Tag>()};

  value_ptr<void> ctx_{};
  const vtable *vtbl_{nullptr};
};

/**
 * @brief Owning wrapper: holds `context_type` by value, sharing lifetime
 * with the handle obtained from @ref ref.
 *
 * @tparam Tag Sink tag identifying a stateful @ref sink_provider_traits
 * specialization. Stateless tags have no per-instance state to own; use
 * `sink_provider_ref{Tag{}}` directly instead.
 */
template <typename Tag> class sink_provider {
public:
  using traits_type = sink_provider_traits<Tag>;
  using context_type = typename traits_type::context_type;
  static_assert(!std::is_void_v<context_type>, "sink_provider<Tag> requires a stateful backend; construct "
                                               "sink_provider_ref{Tag{}} directly for stateless backends");

  /**
   * @brief Constructs the owning wrapper from an already-built context.
   */
  constexpr explicit sink_provider(context_type context) noexcept : context_(std::move(context)) {}

  /**
   * @brief Borrows a @ref sink_provider_ref bound to this wrapper's context.
   *
   * Ref-qualified `&`: calling this on a temporary wrapper is a compile
   * error, since the returned handle would otherwise outlive the context it
   * points into.
   */
  [[nodiscard]] constexpr sink_provider_ref ref() & noexcept RELOCO_LIFETIMEBOUND {
    return sink_provider_ref(Tag{}, context_);
  }

  /**
   * @brief Convenience shorthand for `ref().as_sink()`.
   */
  [[nodiscard]] constexpr sink as_sink() & noexcept RELOCO_LIFETIMEBOUND {
    // Clang can't detect that ref() just forwards our context reference to context_
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
#pragma clang diagnostic ignored "-Wdangling-gsl"
#endif

    return ref().as_sink();

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  }

private:
  context_type context_;
};

// ============================================================================
// Built-in Backends
// ============================================================================

/**
 * @brief Tag for a stateless sink that discards every write.
 *
 * The traits-based counterpart of @ref microfmt::null_sink, demonstrating
 * the `context_type = void` shape from `sink_provider_traits`.
 */
struct null_sink_provider_tag {};

template <> struct sink_provider_traits<null_sink_provider_tag> {
  using context_type = void;

  static void write(microfmt::string_view) noexcept {
    // Intentionally discards every write.
  }
};

/**
 * @brief Tag for a stateful sink that appends into a fixed-size @ref
 * microfmt::span of character storage, truncating writes that exceed its
 * remaining capacity.
 *
 * The traits-based counterpart of @ref microfmt::span_sink, demonstrating
 * the stateful `context_type` shape: @ref span_sink_provider_context tracks
 * a write position that `write` mutates through `value_ref<context_type>`.
 */
struct span_sink_provider_tag {};

struct MICROFMT_API_CLASS span_sink_provider_context {
  span<char> buffer;
  size_t pos{0};
};

template <> struct sink_provider_traits<span_sink_provider_tag> {
  using context_type = span_sink_provider_context;

  static void write(value_ref<context_type> ctx, microfmt::string_view sv) noexcept {
    const size_t avail = (ctx->pos < ctx->buffer.size()) ? (ctx->buffer.size() - ctx->pos) : 0;
    const size_t n = std::min(sv.size(), avail);
    if (n > 0) {
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
      std::copy_n(sv.data(), n, ctx->buffer.data() + ctx->pos);
      RELOCO_END_UNSAFE_BUFFER_USAGE;
      ctx->pos += n;
    }
  }
};

} // namespace microfmt
