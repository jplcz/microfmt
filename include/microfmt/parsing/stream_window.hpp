// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file stream_window.hpp
 * @brief A fixed-size window over input that arrives in pieces, for parsing text that does not fit in memory.
 *
 * Why: `token_stream` normally needs the whole text in one contiguous buffer. `stream_window<N>` keeps only `N`
 * bytes, refills them from a reader (a file, a socket, a UART) and lets the parser drop what it has finished with.
 * `window_ref` is the non-template handle the stream stores, so every parser helper keeps taking `token_stream &`.
 * See `docs/token-stream.md` ("Streaming input").
 */

#include <cstddef>
#include <cstdint>
#include <reloco/array.hpp>
#include <reloco/function_ref.hpp>
#include <reloco/span.hpp>
#include <reloco/string_view.hpp>
#include <reloco/value_ref.hpp>

namespace microfmt {

/** Fills `out` with new input and returns the number of bytes written; 0 means the end of the input. */
using window_reader = reloco::function_ref<size_t(reloco::span<char>)>;

/** Smallest window: four times the largest lookahead margin the stream ever asks for (64 bytes). */
inline constexpr size_t kMinStreamWindow = 256;

/** The shared state behind a window. Copies of a `token_stream` all point at one of these. */
struct RELOCO_POINTER window_state {
  reloco::span<char> buffer;
  window_reader reader;
  size_t used{0};         ///< Bytes of `buffer` holding input.
  size_t base{0};         ///< Absolute offset (bytes since the start of the input) of `buffer[0]`.
  uint32_t generation{0}; ///< Bumped by every discard; stream copies made earlier are stale.
  bool eof{false};        ///< The reader returned 0.
  bool overflowed{false}; ///< A token did not fit; the window is too small for this input.

  window_state(reloco::span<char> storage RELOCO_LIFETIMEBOUND, window_reader source) noexcept
      : buffer(storage), reader(source) {}

  [[nodiscard]] reloco::string_view text() const noexcept { return reloco::string_view(buffer.data(), used); }

  /** Reads until `used >= want`, the window is full or the input ends. */
  void fill(size_t want) noexcept {
    while (!eof && used < want && used < buffer.size()) {
      const size_t room = buffer.size() - used;
      const size_t got = reader(buffer.subspan(used));
      if (got == 0) {
        eof = true;
      } else {
        used += got < room ? got : room;
      }
    }
  }

  /** Drops the first `count` bytes: the rest moves to the front and the base grows. */
  void discard(size_t count) noexcept {
    if (count == 0 || count > used) {
      return;
    }
    for (size_t i = count; i < used; ++i) {
      buffer[i - count] = buffer[i];
    }
    used -= count;
    base += count;
    ++generation;
    overflowed = false;
  }
};

/** Non-template handle to a `stream_window`; what `token_stream` takes. */
class RELOCO_POINTER window_ref {
public:
  explicit window_ref(window_state &state RELOCO_LIFETIMEBOUND) noexcept : state_(state) {}

  [[nodiscard]] window_state &state() const noexcept { return *state_; }

  /** True when a token did not fit in the window since the last discard (the window is too small). */
  [[nodiscard]] bool overflowed() const noexcept { return state().overflowed; }
  /** Absolute offset of the first byte currently in the window. */
  [[nodiscard]] size_t base() const noexcept { return state().base; }
  /** Bytes currently in the window. */
  [[nodiscard]] size_t used() const noexcept { return state().used; }

private:
  reloco::value_ref<window_state> state_;
};

/**
 * @brief `N` bytes of storage plus the bookkeeping for refilling it from a reader. No heap.
 *
 * The window must outlive every `token_stream` built on it and must not move, so it is neither copyable nor
 * movable. Choose `N` for your largest statement: a statement is parsed inside one window, and only
 * `token_stream::discard_consumed()` (usually via `end_statement`) makes room.
 */
template <size_t N> class RELOCO_OWNER stream_window {
  static_assert(N >= kMinStreamWindow, "a stream window needs at least 256 bytes (4 x the largest lookahead margin)");

public:
  /** @param reader `size_t(span<char>)`: fill the span, return the byte count, 0 at the end of the input. */
  explicit stream_window(window_reader reader RELOCO_LIFETIMEBOUND) noexcept
      : state_(reloco::span<char>(buffer_.data(), N), reader) {}

  stream_window(const stream_window &) = delete;
  stream_window &operator=(const stream_window &) = delete;

  [[nodiscard]] window_ref ref() & noexcept RELOCO_LIFETIMEBOUND { return window_ref(state_); }

private:
  reloco::array<char, N> buffer_{};
  window_state state_;
};

} // namespace microfmt
