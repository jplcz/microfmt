#pragma once

/** @file remote_hash_table.hpp
 * @brief Type-erased formatting support for remote chained hash tables. */

#include "remote_container.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Type-erased virtual table for inspecting remote chaining hash tables
 *        (bucket array + singly-linked lists per bucket).
 */
struct remote_hash_table_vtable {
  /**
   * @brief Reads the total number of buckets in the hash table.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the table.
   * @param scratch Reusable scratch storage for remote reads.
   * @param out_count Receives the bucket count.
   * @return `true` on success.
   */
  bool (*get_bucket_count)(const void *state, address_space_ref space,
                           span<std::byte> scratch, size_t &out_count) noexcept;

  /**
   * @brief Gets the first node address for a given bucket index.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the table.
   * @param scratch Reusable scratch storage for remote reads.
   * @param bucket_index Zero-based bucket index.
   * @param out_node_addr Receives the first node address.
   * @return `true` on success.
   */
  bool (*get_bucket_head)(const void *state, address_space_ref space,
                          span<std::byte> scratch, size_t bucket_index,
                          uintptr_t &out_node_addr) noexcept;

  /**
   * @brief Gets the next node address in the current bucket's chain.
   * @param state Caller-defined traversal state.
   * @param space Address space containing the table.
   * @param scratch Reusable scratch storage for remote reads.
   * @param node_addr Current node address.
   * @param out_next_addr Receives the next node address.
   * @return `true` on success.
   */
  bool (*get_next_node)(const void *state, address_space_ref space,
                        span<std::byte> scratch, uintptr_t node_addr,
                        uintptr_t &out_next_addr) noexcept;

  /**
   * @brief Formats a hash table entry (key and/or value) at the given node
   * address respecting container_options (print_key, print_value,
   * kv_separator).
   * @param state Caller-defined traversal state.
   * @param space Address space containing the table.
   * @param scratch Reusable scratch storage for remote reads.
   * @param node_addr Node containing the entry.
   * @param opts Entry rendering options.
   * @param out Destination sink.
   * @return `true` on success.
   */
  bool (*format_entry)(const void *state, address_space_ref space,
                       span<std::byte> scratch, uintptr_t node_addr,
                       const container_options &opts, const sink &out) noexcept;
};

/**
 * @brief Factory helper that binds user state and a remote_hash_table_vtable
 * into a context compatible with make_container_context and
 * remote_container_view.
 * @tparam State Caller-defined state consumed by @p vtable.
 * @param container_addr Remote hash-table address.
 * @param initial_state Initial caller-defined state.
 * @param vtable Operations used to traverse and format entries.
 * @return A context suitable for constructing @ref remote_container_view.
 */
template <typename State>
[[nodiscard]] constexpr auto
make_remote_hash_table_context(uintptr_t container_addr, State initial_state,
                               remote_hash_table_vtable vtable) noexcept {

  struct hash_context_state {
    uintptr_t container_addr;
    State user_state;
    remote_hash_table_vtable vtable;
  };

  return make_container_context(
      hash_context_state{container_addr, initial_state, vtable},
      [](hash_context_state &ctx, const container_options &opts,
         address_space_ref space, span<std::byte> scratch,
         const sink &out) noexcept {
        out.write(opts.open_bracket);

        size_t bucket_count = 0;
        if (!ctx.vtable.get_bucket_count ||
            !ctx.vtable.get_bucket_count(&ctx.user_state, space, scratch,
                                         bucket_count)) {
          out.write(opts.close_bracket);
          return true;
        }

        size_t print_count = 0;

        for (size_t b = 0; b < bucket_count; ++b) {
          uintptr_t current_node = 0;
          if (!ctx.vtable.get_bucket_head ||
              !ctx.vtable.get_bucket_head(&ctx.user_state, space, scratch, b,
                                          current_node)) {
            continue; // Skip faulty or empty buckets
          }

          while (current_node != 0) {
            if (print_count >= opts.max_print) {
              if (print_count > 0)
                out.write(opts.entry_separator);
              out.write("...");
              goto finish;
            }

            if (print_count > 0) {
              out.write(opts.entry_separator);
            }

            if (!ctx.vtable.format_entry ||
                !ctx.vtable.format_entry(&ctx.user_state, space, scratch,
                                         current_node, opts, out)) {
              out.write("<fault>");
              goto finish;
            }

            print_count++;

            uintptr_t next_node = 0;
            if (!ctx.vtable.get_next_node ||
                !ctx.vtable.get_next_node(&ctx.user_state, space, scratch,
                                          current_node, next_node)) {
              break;
            }
            current_node = next_node;
          }
        }

      finish:
        out.write(opts.close_bracket);
        return true;
      });
}

namespace detail {

template <typename Key, typename Value, typename RemotePtr, typename RemoteSize>
struct hash_table_layout_traits_impl {
  struct layout_state {
    uintptr_t container_addr;
    ptrdiff_t buckets_ptr_off;  // Pointer to bucket array
    ptrdiff_t bucket_count_off; // Size_t total buckets
    ptrdiff_t node_next_off;    // Offset to next pointer in node
    ptrdiff_t node_key_off;     // Offset to key in node
    ptrdiff_t node_val_off;     // Offset to value in node
  };

  static constexpr remote_hash_table_vtable vtbl{
      .get_bucket_count =
          [](const void *state, address_space_ref space, span<std::byte>,
             size_t &out_count) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t count_addr =
                detail::add_address_offset(s->container_addr,
                                           s->bucket_count_off);
            RemoteSize remote_sz{};
            if (!space.read(count_addr, remote_sz))
              return false;
            out_count = static_cast<size_t>(remote_sz);
            return true;
          },
      .get_bucket_head =
          [](const void *state, address_space_ref space, span<std::byte>,
             size_t bucket_index, uintptr_t &out_node_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t buckets_ptr_addr =
                detail::add_address_offset(s->container_addr,
                                           s->buckets_ptr_off);
            RemotePtr buckets_ptr{};
            if (!space.read(buckets_ptr_addr, buckets_ptr))
              return false;

            // Each bucket stores a pointer to the first node (or node pointers
            // array)
            uintptr_t bucket_entry_addr = static_cast<uintptr_t>(buckets_ptr) +
                                          (bucket_index * sizeof(RemotePtr));
            RemotePtr head_node_ptr{};
            if (!space.read(bucket_entry_addr, head_node_ptr))
              return false;
            out_node_addr = static_cast<uintptr_t>(head_node_ptr);
            return true;
          },
      .get_next_node =
          [](const void *state, address_space_ref space, span<std::byte>,
             uintptr_t node_addr, uintptr_t &out_next_addr) noexcept {
            const auto *s = static_cast<const layout_state *>(state);
            uintptr_t next_ptr_addr =
                detail::add_address_offset(node_addr, s->node_next_off);
            RemotePtr next_ptr{};
            if (!space.read(next_ptr_addr, next_ptr))
              return false;
            out_next_addr = static_cast<uintptr_t>(next_ptr);
            return true;
          },
      .format_entry =
          [](const void *state, address_space_ref space,
             span<std::byte> scratch, uintptr_t node_addr,
             const container_options &opts, const sink &out) noexcept {
            const auto *s = static_cast<const layout_state *>(state);

            // Format Key (if enabled)
            if (opts.print_key) {
              uintptr_t key_addr =
                  detail::add_address_offset(node_addr, s->node_key_off);
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

            // Format Value (if enabled)
            if (opts.print_value) {
              uintptr_t val_addr =
                  detail::add_address_offset(node_addr, s->node_val_off);
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

} // namespace detail

/**
 * @brief Generates contexts for conventional chained-hash-table layouts.
 */
struct remote_hash_table_traits {
  /**
   * @brief Layout generator for chaining hash tables.
   * @tparam Key Key type stored by each node.
   * @tparam Value Value type stored by each node.
   * @tparam RemotePtr Pointer representation in the target process.
   * @tparam RemoteSize Bucket-count representation in the target process.
   * @param buckets_ptr_offset Offset to the bucket-array pointer.
   * @param bucket_count_offset Offset to the bucket count.
   * @param node_next_offset Offset to a node's next pointer.
   * @param node_key_offset Offset to a node's key.
   * @param node_val_offset Offset to a node's value.
   * @return A callable that binds a remote container address to this layout.
   */
  template <typename Key, typename Value, typename RemotePtr = uintptr_t,
            typename RemoteSize = size_t>
  [[nodiscard]] static constexpr auto
  chaining_layout(ptrdiff_t buckets_ptr_offset, ptrdiff_t bucket_count_offset,
                  ptrdiff_t node_next_offset, ptrdiff_t node_key_offset,
                  ptrdiff_t node_val_offset) noexcept {

    using impl = detail::hash_table_layout_traits_impl<Key, Value, RemotePtr,
                                                       RemoteSize>;

    return [=](uintptr_t container_addr) {
      typename impl::layout_state state{container_addr,      buckets_ptr_offset,
                                        bucket_count_offset, node_next_offset,
                                        node_key_offset,     node_val_offset};
      return make_remote_hash_table_context(container_addr, state, impl::vtbl);
    };
  }
};

} // namespace microfmt
