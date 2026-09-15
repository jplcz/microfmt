// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "remote_container.hpp"
#include "remote_object.hpp"
#include <cstddef>
#include <cstdint>

namespace microfmt {

// ============================================================================
// Remote Binary Tree Vtable & Context Builder
// ============================================================================

/**
 * @brief Type-erased virtual table for inspecting remote binary trees
 *        (BSTs, Red-Black trees, std::map / std::set nodes).
 */
struct remote_binary_tree_vtable {
  /**
   * @brief Extracts the root node address from the tree container.
   */
  bool (*get_root_node)(const void *state, address_space_ref space,
                        span<std::byte> scratch,
                        uintptr_t &out_node_addr) noexcept;

  /**
   * @brief Extracts the left child node address given a node address.
   */
  bool (*get_left_node)(const void *state, address_space_ref space,
                        span<std::byte> scratch, uintptr_t node_addr,
                        uintptr_t &out_left_addr) noexcept;

  /**
   * @brief Extracts the right child node address given a node address.
   */
  bool (*get_right_node)(const void *state, address_space_ref space,
                         span<std::byte> scratch, uintptr_t node_addr,
                         uintptr_t &out_right_addr) noexcept;

  /**
   * @brief Formats the payload/entry at the given node address.
   */
  bool (*format_node)(const void *state, address_space_ref space,
                      span<std::byte> scratch, uintptr_t node_addr,
                      const container_options &opts, const sink &out) noexcept;
};

/**
 * @brief Factory helper that binds user state and a remote_binary_tree_vtable
 * into a context compatible with container_context and remote_container_view.
 *
 * Partitions the provided scratch buffer dynamically: the first half is used
 * as a bounded iteration stack, and the remainder is passed down for node
 * formatting.
 */
template <typename State>
[[nodiscard]] constexpr auto
make_remote_binary_tree_context(uintptr_t container_addr, State initial_state,
                                remote_binary_tree_vtable vtable) noexcept {

  struct tree_context_state {
    uintptr_t container_addr;
    State user_state;
    remote_binary_tree_vtable vtable;
  };

  return make_container_context(
      tree_context_state{container_addr, initial_state, vtable},
      [](tree_context_state &ctx, const container_options &opts,
         address_space_ref space, span<std::byte> scratch,
         const sink &out) noexcept {
        out.write(opts.open_bracket);

        if (scratch.size() < sizeof(uintptr_t) * 4) {
          out.write("<fault>");
          out.write(opts.close_bracket);
          return true;
        }

        // Minimum safety check for scratch size
        if (scratch.size() < sizeof(uintptr_t) * 8) {
          out.write("<fault>");
          out.write(opts.close_bracket);
          return true;
        }

        // Bound the node stack capacity (depth of 64 is enough for 2^64 nodes)
        constexpr size_t kMaxTreeStackDepth = 64;
        size_t available_elements = scratch.size() / sizeof(uintptr_t);
        size_t stack_capacity =
            std::min(available_elements / 2, kMaxTreeStackDepth);
        size_t stack_bytes = stack_capacity * sizeof(uintptr_t);

        if (scratch.size() <= stack_bytes) {
          out.write("<fault>");
          out.write(opts.close_bracket);
          return true;
        }

        uintptr_t *node_stack = reinterpret_cast<uintptr_t *>(scratch.data());
        span<std::byte> element_scratch = scratch.subspan(stack_bytes);

        uintptr_t root_node = 0;
        if (!ctx.vtable.get_root_node ||
            !ctx.vtable.get_root_node(&ctx.user_state, space, element_scratch,
                                      root_node)) {
          out.write(opts.close_bracket);
          return true;
        }

        if (root_node == 0) {
          out.write(opts.close_bracket);
          return true;
        }

        size_t stack_top = 0;
        uintptr_t curr = root_node;
        size_t print_count = 0;

        // Iterative In-Order Traversal using the scratch-backed node stack
        while (curr != 0 || stack_top > 0) {
          while (curr != 0) {
            if (stack_top >= stack_capacity)
              break;
            node_stack[stack_top++] = curr;

            uintptr_t left = 0;
            if (!ctx.vtable.get_left_node ||
                !ctx.vtable.get_left_node(&ctx.user_state, space,
                                          element_scratch, curr, left)) {
              break;
            }
            curr = left;
          }

          if (stack_top == 0)
            break;
          curr = node_stack[--stack_top];

          if (print_count >= opts.max_print) {
            if (print_count > 0)
              out.write(opts.entry_separator);
            out.write("...");
            break;
          }

          if (print_count > 0) {
            out.write(opts.entry_separator);
          }

          if (!ctx.vtable.format_node ||
              !ctx.vtable.format_node(&ctx.user_state, space, element_scratch,
                                      curr, opts, out)) {
            out.write("<fault>");
            break;
          }

          print_count++;

          uintptr_t right = 0;
          if (!ctx.vtable.get_right_node ||
              !ctx.vtable.get_right_node(&ctx.user_state, space,
                                         element_scratch, curr, right)) {
            curr = 0;
          } else {
            curr = right;
          }
        }

        out.write(opts.close_bracket);
        return true;
      });
}

// ============================================================================
// Binary Tree Layout Traits & Generator Helpers
// ============================================================================

template <typename Key, typename Value, typename RemotePtr>
struct binary_tree_layout_traits_impl {
  struct layout_state {
    uintptr_t container_addr;
    ptrdiff_t root_off;
    ptrdiff_t left_off;
    ptrdiff_t right_off;
    ptrdiff_t key_off;
    ptrdiff_t val_off;
  };

  static constexpr remote_binary_tree_vtable vtbl{
      .get_root_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t &out_node_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t root_ptr_addr = s->container_addr + s->root_off;
            RemotePtr remote_ptr{};
            if (!space.read(root_ptr_addr, remote_ptr))
              return false;
            out_node_addr = static_cast<uintptr_t>(remote_ptr);
            return true;
          },
      .get_left_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t node_addr, uintptr_t &out_left_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t left_ptr_addr = node_addr + s->left_off;
            RemotePtr remote_ptr{};
            if (!space.read(left_ptr_addr, remote_ptr))
              return false;
            out_left_addr = static_cast<uintptr_t>(remote_ptr);
            return true;
          },
      .get_right_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t node_addr, uintptr_t &out_right_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t right_ptr_addr = node_addr + s->right_off;
            RemotePtr remote_ptr{};
            if (!space.read(right_ptr_addr, remote_ptr))
              return false;
            out_right_addr = static_cast<uintptr_t>(remote_ptr);
            return true;
          },
      .format_node =
          [](const void *state, address_space_ref space,
             span<std::byte> scratch, uintptr_t node_addr,
             const container_options &opts, const sink &out) noexcept {
            const auto *s = static_cast<const layout_state *>(state);

            if (opts.print_key) {
              uintptr_t key_addr = node_addr + s->key_off;
              if constexpr (remote_object_traits<Key>::is_registered) {
                remote_object_view key_view(key_addr, space, type_tag<Key>{},
                                            scratch);
                formatter<remote_object_view>().format(key_view, out);
              } else {
                if (scratch.size() < sizeof(Key))
                  return false;
                Key *k_ptr = reinterpret_cast<Key *>(scratch.data());
                if (!space.read_bytes(key_addr, k_ptr, sizeof(Key)))
                  return false;
                formatter<Key>().format(*k_ptr, out);
              }
              out.write(opts.kv_separator);
            }

            if (opts.print_value) {
              uintptr_t val_addr = node_addr + s->val_off;
              if constexpr (remote_object_traits<Value>::is_registered) {
                remote_object_view val_view(val_addr, space, type_tag<Value>{},
                                            scratch);
                formatter<remote_object_view>().format(val_view, out);
              } else {
                if (scratch.size() < sizeof(Value))
                  return false;
                Value *v_ptr = reinterpret_cast<Value *>(scratch.data());
                if (!space.read_bytes(val_addr, v_ptr, sizeof(Value)))
                  return false;
                formatter<Value>().format(*v_ptr, out);
              }
            }

            return true;
          }};
};

struct remote_binary_tree_traits {
  /**
   * @brief Layout generator for binary search trees (BSTs, maps, sets).
   *
   * @tparam Key Key type stored in nodes
   * @tparam Value Value type stored in nodes
   * @tparam RemotePtr Target pointer type (e.g., uintptr_t or uint32_t for
   * compat)
   */
  template <typename Key, typename Value, typename RemotePtr = uintptr_t>
  [[nodiscard]] static constexpr auto
  bst_layout(ptrdiff_t root_offset, ptrdiff_t left_offset,
             ptrdiff_t right_offset, ptrdiff_t key_offset,
             ptrdiff_t val_offset) noexcept {

    using impl = binary_tree_layout_traits_impl<Key, Value, RemotePtr>;

    return [=](uintptr_t container_addr) {
      typename impl::layout_state state{container_addr, root_offset,
                                        left_offset,    right_offset,
                                        key_offset,     val_offset};
      return make_remote_binary_tree_context(container_addr, state, impl::vtbl);
    };
  }
};

} // namespace microfmt