#pragma once
#include <microfmt/microfmt.hpp>

namespace microfmt {

/**
 * @brief Output sink that guarantees a null-terminated string within an
 * externally provided, fixed-size memory buffer.
 *
 * Reserves 1 byte for the trailing `\0`. Output is clamped to the buffer's
 * capacity minus 1, and is guaranteed to be null-terminated after every write.
 */
class RELOCO_POINTER c_string_span_sink : private detail::c_string_sink_base {
public:
  /**
   * @brief Constructs a null-terminated string sink over a @ref microfmt::span.
   *
   * @param buf The target character buffer view. If empty, the sink discards
   * all writes safely.
   */
  explicit constexpr c_string_span_sink(span<char> buf RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : detail::c_string_sink_base{buf.empty() ? nullptr : buf.data(), buf.empty() ? 0 : buf.size() - 1, 0} {
    if (m_data) {
      m_data[0] = '\0';
    }
  }

#if RELOCO_HAS_STD_SPAN
  /**
   * @brief Constructs a null-terminated string sink over a @c std::span.
   *
   * @tparam Extent The static extent of the standard span.
   * @param buf The target standard span view.
   */
  template <std::size_t Extent>
  explicit constexpr c_string_span_sink(
      std::span<char, Extent> buf RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : detail::c_string_sink_base{buf.empty() ? nullptr : buf.data(), buf.empty() ? 0 : buf.size() - 1, 0} {
    if (m_data) {
      m_data[0] = '\0';
    }
  }
#endif

  /**
   * @brief Creates a type-erased @ref sink adapter pointing to this instance.
   *
   * @return A lightweight @ref sink struct configured to append and
   * null-terminate.
   */
  [[nodiscard]] sink as_sink() noexcept RELOCO_LIFETIMEBOUND {
    return sink{this, &detail::c_string_sink_base::write_thunk};
  }

  /**
   * @brief Returns a null-terminated C string pointer.
   *
   * @return Pointer to the underlying null-terminated buffer, or a safe empty
   * static string if the initial span was empty.
   */
  [[nodiscard]] constexpr const char *c_str() const noexcept RELOCO_LIFETIMEBOUND { return m_data ? m_data : ""; }

  /**
   * @brief Returns a string view over the characters written (excluding null
   * terminator).
   *
   * @return Non-owning view of formatted text.
   */
  [[nodiscard]] constexpr microfmt::string_view view() const noexcept RELOCO_LIFETIMEBOUND {
    return microfmt::string_view(m_data, m_pos);
  }

  /**
   * @brief Returns the length of the string in bytes (excluding null
   * terminator).
   *
   * @return String length in bytes.
   */
  [[nodiscard]] constexpr std::size_t size() const noexcept { return m_pos; }

  /**
   * @brief Returns the maximum characters the sink can store (excluding null
   * terminator).
   *
   * @return Maximum payload capacity (@c buf.size() - 1).
   */
  [[nodiscard]] constexpr std::size_t max_size() const noexcept { return m_max_payload; }

  /**
   * @brief Resets the sink to an empty, null-terminated state.
   */
  RELOCO_REINITIALIZES constexpr void reset() noexcept {
    m_pos = 0;
    if (m_data) {
      m_data[0] = '\0';
    }
  }
};

} // namespace microfmt
