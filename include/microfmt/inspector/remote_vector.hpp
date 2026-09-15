// ============================================================================
// Generalized Remote Vector / Sequence View & Traits
// ============================================================================

#pragma once

#include "remote_container.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Type-erased virtual table for sequence/vector-like remote containers.
 *
 * Uses raw function pointers to inspect sizes, capacities, and element
 * addresses without polluting headers with concrete container templates.
 */
struct remote_vector_vtable {
  /**
   * @brief Reads the total number of elements in the remote container.
   */
  bool (*get_size)(const void *ctx, address_space_ref space,
                   span<std::byte> scratch, size_t &out_size) noexcept;

  /**
   * @brief Reads the container's capacity (optional).
   */
  bool (*get_capacity)(const void *ctx, address_space_ref space,
                       span<std::byte> scratch, size_t &out_cap) noexcept;

  /**
   * @brief Computes or resolves the remote address of the N-th element.
   */
  bool (*get_element_address)(const void *ctx, address_space_ref space,
                              span<std::byte> scratch, size_t index,
                              uintptr_t &out_elem_addr) noexcept;

  /**
   * @brief Formats a single element at the given remote address into the sink
   *        using the scratch buffer for safe local reading.
   */
  bool (*format_element)(const void *ctx, address_space_ref space,
                         span<std::byte> scratch, uintptr_t elem_addr,
                         const sink &out) noexcept;
};

/**
 * @brief Concrete helper that links a user context to the vector vtable.
 */
template <typename State>
[[nodiscard]] constexpr auto
make_remote_vector_context(uintptr_t container_addr, State initial_state,
                           remote_vector_vtable vtable) noexcept {

  struct vector_context_state {
    uintptr_t container_addr;
    State user_state;
    remote_vector_vtable vtable;
  };

  return make_container_context(
      vector_context_state{container_addr, initial_state, vtable},
      [](vector_context_state &ctx, const container_options &opts,
         address_space_ref space, span<std::byte> scratch,
         const sink &out) noexcept {
        size_t size = 0;
        if (ctx.vtable.get_size &&
            !ctx.vtable.get_size(&ctx.user_state, space, scratch, size)) {
          out.write(opts.open_bracket);
          out.write(opts.close_bracket);
          return true;
        }

        out.write(opts.open_bracket);
        size_t print_count = (size < opts.max_print) ? size : opts.max_print;

        for (size_t i = 0; i < print_count; ++i) {
          if (i > 0) {
            out.write(opts.entry_separator);
          }

          uintptr_t elem_addr = 0;
          if (!ctx.vtable.get_element_address ||
              !ctx.vtable.get_element_address(&ctx.user_state, space, scratch,
                                              i, elem_addr)) {
            out.write("<fault>");
            break;
          }

          if (!ctx.vtable.format_element ||
              !ctx.vtable.format_element(&ctx.user_state, space, scratch,
                                         elem_addr, out)) {
            out.write("<fault>");
            break;
          }
        }

        if (size > opts.max_print) {
          out.write(opts.entry_separator);
          out.write("...");
        }

        out.write(opts.close_bracket);
        return true;
      });
}

namespace detail {

template <typename T, typename RemotePtr, typename RemoteSize>
struct vector_layout_traits_impl {
  struct layout_state {
    uintptr_t container_addr;
    ptrdiff_t d_off;
    ptrdiff_t s_off;
    ptrdiff_t c_off;
  };

  static constexpr inline remote_vector_vtable vtbl{
      .get_size =
          [](const void *state, address_space_ref space, span<std::byte>,
             size_t &out_size) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t size_addr = s->container_addr + s->s_off;
            RemoteSize remote_sz{};
            if (!space.read(size_addr, remote_sz))
              return false;
            out_size = static_cast<size_t>(remote_sz);
            return true;
          },
      .get_capacity =
          [](const void *state, address_space_ref space, span<std::byte>,
             size_t &out_cap) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            if (s->c_off < 0)
              return false;
            uintptr_t cap_addr = s->container_addr + s->c_off;
            RemoteSize remote_cap{};
            if (!space.read(cap_addr, remote_cap))
              return false;
            out_cap = static_cast<size_t>(remote_cap);
            return true;
          },
      .get_element_address =
          [](const void *state, address_space_ref space, span<std::byte>,
             size_t index, uintptr_t &out_elem_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t data_ptr_addr = s->container_addr + s->d_off;
            RemotePtr remote_ptr{};
            if (!space.read(data_ptr_addr, remote_ptr))
              return false;
            out_elem_addr =
                static_cast<uintptr_t>(remote_ptr) + (index * sizeof(T));
            return true;
          },
      .format_element =
          [](const void *, address_space_ref space, span<std::byte> scratch,
             uintptr_t elem_addr, const sink &out) noexcept {
            if constexpr (remote_object_traits<T>::is_registered) {
              remote_object_view obj_view(elem_addr, space, type_tag<T>{},
                                          scratch);
              formatter<remote_object_view> fmt;
              fmt.format(obj_view, out);
            } else {
              if (scratch.size() < sizeof(T))
                return false;
              T *val_ptr = reinterpret_cast<T *>(scratch.data());
              if (!space.read_bytes(elem_addr, val_ptr, sizeof(T)))
                return false;
              formatter<T> fmt;
              fmt.format(*val_ptr, out);
            }
            return true;
          }};
};

// Implementation helper for C-style array layouts
template <typename T> struct carray_layout_traits_impl {
  struct carray_state {
    uintptr_t array_addr;
    size_t count;
  };

  static constexpr remote_vector_vtable vtbl{
      .get_size =
          [](const void *state, address_space_ref, span<std::byte>,
             size_t &out_size) noexcept {
            const auto *s = static_cast<const carray_state *>(state);
            out_size = s->count;
            return true;
          },
      .get_capacity =
          [](const void *state, address_space_ref, span<std::byte>,
             size_t &out_cap) noexcept {
            const auto *s = static_cast<const carray_state *>(state);
            out_cap = s->count;
            return true;
          },
      .get_element_address =
          [](const void *state, address_space_ref, span<std::byte>,
             size_t index, uintptr_t &out_elem_addr) noexcept {
            const auto *s = static_cast<const carray_state *>(state);
            if (index >= s->count)
              return false;
            out_elem_addr = s->array_addr + (index * sizeof(T));
            return true;
          },
      .format_element =
          [](const void *, address_space_ref space, span<std::byte> scratch,
             uintptr_t elem_addr, const sink &out) noexcept {
            if constexpr (remote_object_traits<T>::is_registered) {
              remote_object_view obj_view(elem_addr, space, type_tag<T>{},
                                          scratch);
              formatter<remote_object_view> fmt;
              fmt.format(obj_view, out);
            } else {
              if (scratch.size() < sizeof(T))
                return false;
              T *val_ptr = reinterpret_cast<T *>(scratch.data());
              if (!space.read_bytes(elem_addr, val_ptr, sizeof(T)))
                return false;
              formatter<T> fmt;
              fmt.format(*val_ptr, out);
            }
            return true;
          }};
};

} // namespace detail

struct remote_vector_traits {
  template <typename T, typename RemotePtr = uintptr_t,
            typename RemoteSize = size_t>
  [[nodiscard]] static constexpr auto
  vector_layout(ptrdiff_t data_offset, ptrdiff_t size_offset,
                ptrdiff_t capacity_offset = -1) noexcept {

    using impl = detail::vector_layout_traits_impl<T, RemotePtr, RemoteSize>;

    return [=](uintptr_t container_addr) {
      typename impl::layout_state state{container_addr, data_offset,
                                        size_offset, capacity_offset};
      return make_remote_vector_context(container_addr, state, impl::vtbl);
    };
  }

  template <typename T>
  [[nodiscard]] static constexpr auto
  carray_layout(size_t fixed_size) noexcept {
    using impl = detail::carray_layout_traits_impl<T>;

    return [=](uintptr_t array_addr) {
      typename impl::carray_state state{array_addr, fixed_size};
      return make_remote_vector_context(array_addr, state, impl::vtbl);
    };
  }
};

} // namespace microfmt
