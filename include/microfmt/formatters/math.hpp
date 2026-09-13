#pragma once

#include "../microfmt.hpp"
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace microfmt {

// ============================================================================
// Vector View (Vec2, Vec3, Vec4, N-dim)
// ============================================================================

template <typename T, size_t N> struct vec_view {
  span<const T> data;
};

template <typename T> struct vec3_view {
  T values[3];
};

template <typename T, size_t N>
[[nodiscard]] constexpr auto vec(const T (&arr)[N]) noexcept {
  return vec_view<T, N>{span<const T>(arr, N)};
}

template <typename T>
[[nodiscard]] constexpr auto vec3(const T &x, const T &y, const T &z) noexcept {
  return vec3_view<T>{{x, y, z}};
}

template <typename T, size_t N> struct formatter<vec_view<T, N>> {
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const vec_view<T, N> &v, const sink &out) const noexcept {
    out.put('<');
    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    for (size_t i = 0; i < v.data.size(); ++i) {
      if (i > 0)
        out.write(", ");
      elem_fmt.format(v.data[i], out);
    }
    out.put('>');
  }
};

template <typename T> struct formatter<vec3_view<T>> {
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const vec3_view<T> &v, const sink &out) const noexcept {
    out.put('<');
    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    for (size_t i = 0; i < 3; ++i) {
      if (i > 0)
        out.write(", ");
      elem_fmt.format(v.values[i], out);
    }
    out.put('>');
  }
};

// ============================================================================
// Matrix View (Rows x Columns)
// ============================================================================

template <typename T, size_t Rows, size_t Cols> struct matrix_view {
  span<const T> data; // Contiguous row-major storage (Rows * Cols)
};

template <typename T, size_t Rows, size_t Cols>
[[nodiscard]] constexpr auto mat(const T (&arr)[Rows * Cols]) noexcept {
  return matrix_view<T, Rows, Cols>{span<const T>(arr, Rows * Cols)};
}

template <typename T, size_t Rows, size_t Cols>
struct formatter<matrix_view<T, Rows, Cols>> {
  std::string_view forwarded_spec{""};

  constexpr void parse(format_parse_context &ctx) noexcept {
    forwarded_spec = ctx.spec();
  }

  void format(const matrix_view<T, Rows, Cols> &m,
              const sink &out) const noexcept {
    out.write("[\n");
    formatter<T> elem_fmt;
    format_parse_context elem_ctx(forwarded_spec);
    elem_fmt.parse(elem_ctx);

    for (size_t r = 0; r < Rows; ++r) {
      out.write("  [");
      for (size_t c = 0; c < Cols; ++c) {
        if (c > 0)
          out.write(", ");
        elem_fmt.format(m.data[r * Cols + c], out);
      }
      out.write("]\n");
    }
    out.put(']');
  }
};

} // namespace microfmt