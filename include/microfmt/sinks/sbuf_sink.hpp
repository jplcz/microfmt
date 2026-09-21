#pragma once
#include <microfmt/microfmt.hpp>

namespace microfmt {

/**
 * @brief Output sink that writes formatted characters into a FreeBSD-style sbuf.
 *
 * Wraps an externally provided sbuf-like pointer and exposes a non-owning,
 * zero-allocation @ref sink interface. Writes are appended safely using the
 * environment's @c sbuf_bcat function.
 *
 * By being templated on the buffer type, this class introduces zero hard
 * dependencies on <sys/sbuf.h> unless instantiated.
 *
 * @tparam SBufT The opaque buffer struct (typically @c struct @c sbuf).
 */
template <typename SBufT> class RELOCO_POINTER sbuf_sink {
public:
  /**
   * @brief Constructs an sbuf sink over an existing buffer pointer.
   *
   * @param sb The target sbuf pointer.
   */
  explicit constexpr sbuf_sink(SBufT *sb RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept : m_sbuf(sb) {}

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured with a callback to append
   * to this sbuf.
   */
  [[nodiscard]] sink as_sink() const noexcept RELOCO_LIFETIMEBOUND {
    return sink{.ctx = m_sbuf, .write_fn = [](void *ctx, microfmt::string_view sv) noexcept {
                  auto *self = static_cast<SBufT *>(ctx);
                  if (!sv.empty()) {
                    // ADL / deferred lookup will resolve sbuf_bcat at instantiation
                    // time if <sys/sbuf.h> is included by the user.
                    sbuf_bcat(self, sv.data(), sv.size());
                  }
                }};
  }

  /**
   * @brief Returns the underlying sbuf pointer.
   *
   * @return The wrapped sbuf instance.
   */
  [[nodiscard]] constexpr SBufT *native_handle() const noexcept { return m_sbuf; }

private:
  SBufT *m_sbuf;
};

} // namespace microfmt
