// ============================================================================
// Type-Erased Remote Forward List / Linked List View & Traits
// ============================================================================

#pragma once

/** @file remote_forward_list.hpp
 * @brief Type-erased formatting support for remote singly-linked lists. */

#include "remote_container.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Type-erased virtual table for inspecting remote linked lists
 *        (singly-linked lists, forward lists, etc.).
 */
struct remote_forward_list_vtable {
  /**
   * @brief Extracts the address of the first node (head) from the container.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the list.
   * @param scratch Reusable scratch storage for remote reads.
   * @param out_node_addr Receives the head-node address.
   * @return `true` on success.
   */
  bool (*get_head_node)(const void *state, address_space_ref space,
                        span<std::byte> scratch,
                        uintptr_t &out_node_addr) noexcept;

  /**
   * @brief Extracts the address of the next node given the current node's
   * address.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the list.
   * @param scratch Reusable scratch storage for remote reads.
   * @param node_addr Current node address.
   * @param out_next_addr Receives the next-node address.
   * @return `true` on success.
   */
  bool (*get_next_node)(const void *state, address_space_ref space,
                        span<std::byte> scratch, uintptr_t node_addr,
                        uintptr_t &out_next_addr) noexcept;

  /**
   * @brief Reads and formats the element stored in the given node into the
   * sink.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the list.
   * @param scratch Reusable scratch storage for remote reads.
   * @param node_addr Node containing the element.
   * @param out Destination sink.
   * @return `true` on success.
   */
  bool (*format_node_element)(const void *state, address_space_ref space,
                              span<std::byte> scratch, uintptr_t node_addr,
                              const sink &out) noexcept;
};

/**
 * @brief Factory helper that binds user state and a remote_forward_list_vtable
 * into a context compatible with make_container_context and
 * remote_container_view.
 * @tparam State Caller-defined state consumed by @p vtable.
 * @param container_addr Remote list-container address.
 * @param initial_state Initial caller-defined state.
 * @param vtable Operations used to traverse and format list nodes.
 * @return A context suitable for constructing @ref remote_container_view.
 */
template <typename State>
[[nodiscard]] constexpr auto
make_remote_forward_list_context(uintptr_t container_addr, State initial_state,
                                 remote_forward_list_vtable vtable) noexcept {

  struct list_context_state {
    uintptr_t container_addr;
    State user_state;
    remote_forward_list_vtable vtable;
  };

  return make_container_context(
      list_context_state{container_addr, initial_state, vtable},
      [](list_context_state &ctx, const container_options &opts,
         address_space_ref space, span<std::byte> scratch,
         const sink &out) noexcept {
        out.write(opts.open_bracket);

        uintptr_t current_node = 0;
        if (!ctx.vtable.get_head_node ||
            !ctx.vtable.get_head_node(&ctx.user_state, space, scratch,
                                      current_node)) {
          out.write(opts.close_bracket);
          return true;
        }

        size_t print_count = 0;

        while (current_node != 0) {
          if (print_count >= opts.max_print) {
            if (print_count > 0)
              out.write(opts.entry_separator);
            out.write("...");
            break;
          }

          if (print_count > 0) {
            out.write(opts.entry_separator);
          }

          if (!ctx.vtable.format_node_element ||
              !ctx.vtable.format_node_element(&ctx.user_state, space, scratch,
                                              current_node, out)) {
            out.write("<fault>");
            break;
          }

          // Advance to the next node
          uintptr_t next_node = 0;
          if (!ctx.vtable.get_next_node ||
              !ctx.vtable.get_next_node(&ctx.user_state, space, scratch,
                                        current_node, next_node)) {
            out.write("<fault>");
            break;
          }

          current_node = next_node;
          print_count++;
        }

        out.write(opts.close_bracket);
        return true;
      });
}

namespace detail {

template <typename T, typename RemotePtr>
struct forward_list_layout_traits_impl {
  struct layout_state {
    uintptr_t container_addr;
    ptrdiff_t head_off;
    ptrdiff_t next_off;
    ptrdiff_t data_off;
  };

  static constexpr remote_forward_list_vtable vtbl{
      .get_head_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t &out_node_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t head_ptr_addr = s->container_addr + s->head_off;
            RemotePtr remote_ptr{};
            if (!space.read(head_ptr_addr, remote_ptr))
              return false;
            out_node_addr = static_cast<uintptr_t>(remote_ptr);
            return true;
          },
      .get_next_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t node_addr, uintptr_t &out_next_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t next_ptr_addr = node_addr + s->next_off;
            RemotePtr remote_ptr{};
            if (!space.read(next_ptr_addr, remote_ptr))
              return false;
            out_next_addr = static_cast<uintptr_t>(remote_ptr);
            return true;
          },
      .format_node_element =
          [](const void *state, address_space_ref space,
             span<std::byte> scratch, uintptr_t node_addr,
             const sink &out) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t elem_addr = node_addr + s->data_off;

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

/**
 * @brief Generates contexts for conventional singly-linked-list layouts.
 */
struct remote_forward_list_traits {
  /**
   * @brief Layout generator for singly-linked lists.
   *
   * @tparam T Element type stored in the node
   * @tparam RemotePtr Target pointer type (e.g. uintptr_t or uint32_t for
   * compat)
   * @param head_offset Offset from container to the head node pointer
   * @param next_offset Offset from a node to its next node pointer
   * @param data_offset Offset from a node to its payload data member
   * @return A callable that binds a remote container address to this layout.
   */
  template <typename T, typename RemotePtr = uintptr_t>
  [[nodiscard]] static constexpr auto
  forward_list_layout(ptrdiff_t head_offset, ptrdiff_t next_offset,
                      ptrdiff_t data_offset) noexcept {

    using impl = detail::forward_list_layout_traits_impl<T, RemotePtr>;

    return [=](uintptr_t container_addr) {
      typename impl::layout_state state{container_addr, head_offset,
                                        next_offset, data_offset};
      return make_remote_forward_list_context(container_addr, state,
                                              impl::vtbl);
    };
  }
};

} // namespace microfmt