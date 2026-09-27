// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <algorithm>
#include <cstring>
#include <microfmt/log/sink.hpp>
#include <reloco/ring_buffer.hpp>

namespace microfmt::log {

/**
 * @brief Binary framing header. Embedding the logger_name directly
 * guarantees we only have one dynamic payload span to append.
 */
struct alignas(4) ring_buffer_log_header {
  uint16_t payload_len;    // Max 65,535 bytes
  uint8_t logger_name_len; // Max 64 bytes
  uint8_t lvl;             // Severity level encoded as an 8-bit integer
};

class ring_buffer_log_sink;

struct ring_buffer_log_sink_tag {};

template <> struct log_sink_traits<ring_buffer_log_sink_tag> {
  using context_type = ring_buffer_log_sink;

  static void log(value_ref<context_type> ctx, const log_msg &msg) noexcept;
};

/**
 * @brief A log sink that serializes structured logs into a byte-oriented `ring_buffer`.
 * Uses `try_write_frame_evicting` to safely auto-evict oldest frames without shredding data.
 */
class RELOCO_POINTER ring_buffer_log_sink {
public:
  explicit constexpr ring_buffer_log_sink(
      reloco::detail::unowned_ring_base<char> &ring RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ring_(&ring) {}

  [[nodiscard]] log_sink as_sink() noexcept RELOCO_LIFETIMEBOUND { return log_sink(ring_buffer_log_sink_tag{}, *this); }

  void log_impl(const log_msg &msg) const noexcept {
    // Enforce limits via truncation
    const std::size_t name_len = std::min<std::size_t>(msg.logger_name.size(), 64);
    const std::size_t payload_len =
        std::min<std::size_t>(msg.payload.size(), static_cast<size_t>(std::numeric_limits<uint16_t>::max()));

    // Pack the 4-byte header
    ring_buffer_log_header hdr{static_cast<uint16_t>(payload_len), static_cast<uint8_t>(name_len),
                               static_cast<uint8_t>(msg.lvl)};

    // Validator derives total_bytes dynamically!
    auto validator = [](const ring_buffer_log_header &h) -> std::pair<bool, std::size_t> {
      return {true, sizeof(ring_buffer_log_header) + h.logger_name_len + h.payload_len};
    };

    const auto spans = array{reloco::span<const char>(msg.logger_name.data(), name_len),
                             reloco::span<const char>(msg.payload.data(), payload_len)};

    // Write atomically and evict oldest if necessary
    std::ignore = ring_->try_write_frame_evicting(hdr, spans, validator);
  }

private:
  reloco::detail::unowned_ring_base<char> *ring_;
};

inline void log_sink_traits<ring_buffer_log_sink_tag>::log(const value_ref<context_type> ctx,
                                                           const log_msg &msg) noexcept {
  ctx->log_impl(msg);
}

/**
 * @brief Represents string data that may cross the circular array boundary.
 */
struct split_string_view {
  reloco::span<const char> first;
  reloco::span<const char> second;

  [[nodiscard]] constexpr std::size_t size() const noexcept { return first.size() + second.size(); }
};

/**
 * @brief RAII Transaction holding an unconsumed log record.
 */
class RELOCO_POINTER RELOCO_CONSUMABLE(unconsumed) log_record_tx {
  reloco::detail::unowned_ring_base<char> *ring_{nullptr};
  level lvl_{level::trace};
  split_string_view logger_name_{};
  split_string_view payload_{};
  std::size_t total_bytes_{0};

  friend class ring_buffer_log_reader;

  log_record_tx(reloco::detail::unowned_ring_base<char> *ring, level lvl, split_string_view name,
                split_string_view payload, std::size_t total_bytes) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : ring_(ring), lvl_(lvl), logger_name_(name), payload_(payload), total_bytes_(total_bytes) {}

public:
  // Creates an empty/exhausted transaction state
  constexpr log_record_tx() noexcept RELOCO_RETURN_TYPESTATE(consumed) = default;

  log_record_tx(const log_record_tx &) = delete;
  log_record_tx &operator=(const log_record_tx &) = delete;

  log_record_tx(log_record_tx &&other) noexcept RELOCO_RETURN_TYPESTATE(unconsumed)
      : ring_(other.ring_), lvl_(other.lvl_), logger_name_(other.logger_name_), payload_(other.payload_),
        total_bytes_(other.total_bytes_) {
    other.ring_ = nullptr; // Steal ownership
  }

  ~log_record_tx() = default; // Implicit rollback on destruction

  [[nodiscard]] constexpr explicit operator bool() const noexcept RELOCO_TEST_TYPESTATE(unconsumed) {
    return ring_ != nullptr;
  }

  [[nodiscard]] level get_level() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) { return lvl_; }
  [[nodiscard]] split_string_view logger_name() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) { return logger_name_; }
  [[nodiscard]] split_string_view payload() const noexcept RELOCO_CALLABLE_WHEN(unconsumed) { return payload_; }

  /**
   * @brief Consumes the bytes from the underlying ring buffer permanently.
   */
  void commit() noexcept RELOCO_SET_TYPESTATE(consumed) {
    if (ring_) {
      ring_->consume(total_bytes_);
      ring_ = nullptr;
    }
  }

  /**
   * @brief Aborts the transaction without consuming bytes (also happens implicitly on destruction).
   */
  void rollback() noexcept RELOCO_SET_TYPESTATE(consumed) { ring_ = nullptr; }
};

/**
 * @brief Pull-based reader for extracting structured logs via RAII transactions.
 * Natively supports the reloco/rust functional iterator pipeline.
 */
class RELOCO_POINTER ring_buffer_log_reader : public reloco::iterator_adaptor<ring_buffer_log_reader, log_record_tx> {
public:
  using item_type = log_record_tx;

  explicit constexpr ring_buffer_log_reader(
      reloco::detail::unowned_ring_base<char> &ring RELOCO_LIFETIMEBOUND RELOCO_LIFETIME_CAPTURE_BY_THIS) noexcept
      : ring_(&ring) {}

  /**
   * @brief Attempts to extract the next log frame.
   * @return A valid log_record_tx if a frame exists, or an empty/false transaction if empty.
   */
  [[nodiscard]] log_record_tx try_read() & noexcept {
    const std::size_t header_elems = sizeof(ring_buffer_log_header);

    if (ring_->size() < header_elems) {
      return {};
    }

    auto hdr_opt = ring_->try_peek_object<ring_buffer_log_header>();
    if (!hdr_opt) {
      return {};
    }

    std::size_t total_bytes = header_elems + hdr_opt->logger_name_len + hdr_opt->payload_len;

    // Self-heal from massive corruption dynamically
    if (total_bytes > ring_->capacity() || hdr_opt->logger_name_len > 64) {
      ring_->clear();
      return {};
    }

    // Wait for the full frame to arrive
    if (ring_->size() < total_bytes) {
      return {};
    }

    // Read slices exactly up to total_bytes
    auto [p1, p2] = ring_->read_slices(total_bytes);

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    // Helper to perfectly slice out split spans using mathematical offsets
    auto slice_spans = [p1, p2](std::size_t offset, std::size_t len) {
      std::size_t p1_avail = (offset < p1.size()) ? p1.size() - offset : 0;
      std::size_t out1_len = std::min(len, p1_avail);

      std::size_t out2_len = len - out1_len;
      std::size_t s2_offset = (offset > p1.size()) ? offset - p1.size() : 0;

      return split_string_view{reloco::span<const char>(p1.data() + offset, out1_len),
                               reloco::span<const char>(p2.data() + s2_offset, out2_len)};
    };

    RELOCO_END_UNSAFE_BUFFER_USAGE;

    return log_record_tx(ring_, static_cast<level>(hdr_opt->lvl), slice_spans(header_elems, hdr_opt->logger_name_len),
                         slice_spans(header_elems + hdr_opt->logger_name_len, hdr_opt->payload_len), total_bytes);
  }

  /**
   * @brief The magic hook that fuels the reloco::iterator_adaptor pipeline!
   */
  [[nodiscard]] reloco::optional<log_record_tx> next_impl() noexcept {
    if (auto tx = try_read()) {
      return reloco::optional(std::move(tx));
    }
    return reloco::nullopt;
  }

private:
  reloco::detail::unowned_ring_base<char> *ring_;
};

} // namespace microfmt::log

template <> struct microfmt::formatter<microfmt::log::split_string_view> {
  static constexpr void parse(microfmt::format_parse_context &) noexcept {}

  static void format(const microfmt::log::split_string_view &val, const microfmt::sink &out) noexcept {
    if (!val.first.empty()) {
      out.write(microfmt::string_view(val.first.data(), val.first.size()));
    }
    if (!val.second.empty()) {
      out.write(microfmt::string_view(val.second.data(), val.second.size()));
    }
  }
};