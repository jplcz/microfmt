// ============================================================================
// Generalized Remote Vector / Sequence View & Traits
// ============================================================================

#pragma once

#include "remote_container.hpp"

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

} // namespace microfmt
