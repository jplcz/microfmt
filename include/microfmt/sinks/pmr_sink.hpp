#pragma once

#include "../microfmt.hpp"
#include <cassert>
#include <cstddef>
#include <memory_resource>
#include <new>
#include <string>
#include <string_view>

namespace microfmt::pmr {

// ============================================================================
// PMR Typedef Aliases
// ============================================================================

using string = std::pmr::string;
using vector = std::pmr::vector<char>;

// ============================================================================
// PMR String Factory
// ============================================================================

template <typename... Args>
[[nodiscard]] std::pmr::string format(std::pmr::memory_resource *mr,
                                      std::string_view fmt_str,
                                      const Args &...args) {
  std::pmr::string result(mr);

  auto out_sink = sink{
      &result, [](void *ctx, std::string_view sv) noexcept {
        auto *str = static_cast<std::pmr::string *>(ctx);
        str->append(sv.data(), sv.size());
      }};

  format_to(out_sink, fmt_str, args...);
  return result;
}

// ============================================================================
// PMR Vector Factory
// ============================================================================

template <typename... Args>
[[nodiscard]] std::pmr::vector<char>
format_vector(std::pmr::memory_resource *mr, std::string_view fmt_str,
              const Args &...args) {
  std::pmr::vector<char> result(mr);

  auto out_sink = sink{
      &result, [](void *ctx, std::string_view sv) noexcept {
        auto *vec = static_cast<std::pmr::vector<char> *>(ctx);
        vec->insert(vec->end(), sv.begin(), sv.end());
      }};

  format_to(out_sink, fmt_str, args...);
  return result;
}

// ============================================================================
// PMR Monotonic Arena Sink (Direct Stream Allocator)
// ============================================================================

class arena_sink {
public:
  explicit arena_sink(std::pmr::memory_resource *mr,
                      std::size_t initial_chunk_size = 128)
      : mr_(mr) {
    assert(mr_ != nullptr);
    assert(initial_chunk_size != 0);
    allocate_chunk(initial_chunk_size);
  }

  ~arena_sink() noexcept { release_chunks(); }

  arena_sink(const arena_sink &) = delete;
  arena_sink &operator=(const arena_sink &) = delete;
  arena_sink(arena_sink &&other) noexcept
      : mr_(other.mr_), current_chunk_(other.current_chunk_),
        size_(other.size_), current_cap_(other.current_cap_) {
    other.current_chunk_ = nullptr;
    other.size_ = 0;
    other.current_cap_ = 0;
  }

  arena_sink &operator=(arena_sink &&other) noexcept {
    if (this != &other) {
      release_chunks();
      mr_ = other.mr_;
      current_chunk_ = other.current_chunk_;
      size_ = other.size_;
      current_cap_ = other.current_cap_;
      other.current_chunk_ = nullptr;
      other.size_ = 0;
      other.current_cap_ = 0;
    }
    return *this;
  }

  [[nodiscard]] sink as_sink() noexcept {
    return sink{this, [](void *ctx, std::string_view sv) noexcept {
                  static_cast<arena_sink *>(ctx)->write(sv);
                }};
  }

  void put(char c) {
    if (size_ >= current_cap_) {
      grow();
    }
    current_chunk_->data[size_++] = c;
  }

  void write(std::string_view sv) {
    for (char c : sv) {
      put(c);
    }
  }

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(current_chunk_->data, size_);
  }

private:
  struct chunk {
    chunk *previous;
    std::size_t capacity;
    char data[1];
  };

  [[nodiscard]] static std::size_t allocation_size(
      std::size_t capacity) noexcept {
    return sizeof(chunk) - sizeof(char) + capacity;
  }

  void allocate_chunk(std::size_t capacity) {
    void *storage = mr_->allocate(allocation_size(capacity), alignof(chunk));
    current_chunk_ = ::new (storage) chunk{current_chunk_, capacity, {}};
    current_cap_ = capacity;
    size_ = 0;
  }

  void grow() {
    const std::size_t new_capacity = current_cap_ * 2;
    chunk *previous_chunk = current_chunk_;
    const std::size_t previous_size = size_;
    allocate_chunk(new_capacity);
    for (std::size_t i = 0; i < previous_size; ++i) {
      current_chunk_->data[i] = previous_chunk->data[i];
    }
    size_ = previous_size;
  }

  void release_chunks() noexcept {
    while (current_chunk_ != nullptr) {
      chunk *previous = current_chunk_->previous;
      const std::size_t capacity = current_chunk_->capacity;
      current_chunk_->~chunk();
      mr_->deallocate(current_chunk_, allocation_size(capacity),
                      alignof(chunk));
      current_chunk_ = previous;
    }
  }

  std::pmr::memory_resource *mr_;
  chunk *current_chunk_{nullptr};
  std::size_t size_{0};
  std::size_t current_cap_{0};
};

} // namespace microfmt::pmr