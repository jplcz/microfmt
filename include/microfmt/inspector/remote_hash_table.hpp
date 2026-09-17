// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file remote_hash_table.hpp
 * @brief Traits-based formatting support for remote chained hash tables. */

#include "remote_container.hpp"
#include "remote_layout_accessor.hpp"
#include "remote_object.hpp"

namespace microfmt {

/**
 * @brief Static customization point for a remote chained hash table.
 *
 * Specializations declare `context_type` and provide `get_bucket_count`,
 * `get_bucket_head`, `get_next_node`, and `format_entry`.
 */
template <typename Tag> struct remote_hash_table_traits;

namespace detail {

template <typename Tag> struct remote_hash_table_dispatch {
  using traits_type = remote_hash_table_traits<Tag>;
  using context_type = typename traits_type::context_type;

  static bool format(value_ref<const context_type> context, uintptr_t container_addr,
                     const container_options &opts, address_space_ref space,
                     span<std::byte> scratch, const sink &out) noexcept {
    out.write(opts.open_bracket);

    size_t bucket_count = 0;
    if (!traits_type::get_bucket_count(context, container_addr, space, scratch,
                                       bucket_count)) {
      out.write(opts.close_bracket);
      return true;
    }

    size_t print_count = 0;
    for (size_t bucket = 0; bucket < bucket_count; ++bucket) {
      uintptr_t current_node = 0;
      if (!traits_type::get_bucket_head(context, container_addr, space,
                                        scratch, bucket, current_node))
        continue;

      while (current_node != 0) {
        if (print_count >= opts.max_print) {
          if (print_count > 0)
            out.write(opts.entry_separator);
          out.write("...");
          goto finish;
        }
        if (print_count > 0)
          out.write(opts.entry_separator);

        if (!traits_type::format_entry(context, space, scratch, current_node,
                                       opts, out)) {
          out.write("<fault>");
          goto finish;
        }
        ++print_count;

        uintptr_t next_node = 0;
        if (!traits_type::get_next_node(context, space, scratch, current_node,
                                        next_node))
          break;
        current_node = next_node;
      }
    }

  finish:
    out.write(opts.close_bracket);
    return true;
  }
};

template <typename Key, typename Value, typename RemotePtr,
          typename RemoteSize>
struct hash_table_layout_tag {};

} // namespace detail

template <typename Tag>
using remote_hash_table =
    basic_remote_container<Tag, detail::remote_hash_table_dispatch<Tag>>;

template <typename Tag>
[[nodiscard]] constexpr remote_hash_table<Tag>
make_remote_hash_table(
    uintptr_t container_addr,
    typename remote_hash_table_traits<Tag>::context_type context) noexcept {
  return remote_hash_table<Tag>(container_addr, std::move(context));
}

template <typename Key, typename Value, typename RemotePtr,
          typename RemoteSize>
struct remote_hash_table_traits<
    detail::hash_table_layout_tag<Key, Value, RemotePtr, RemoteSize>> {
  using pointer_query =
      decltype(make_remote_offset_query<uintptr_t, RemotePtr>(0));
  using size_query = decltype(make_remote_offset_query<size_t, RemoteSize>(0));

  struct context_type {
    pointer_query buckets;
    size_query bucket_count;
    pointer_query next;
    ptrdiff_t key_offset;
    ptrdiff_t value_offset;
  };

  static bool get_bucket_count(value_ref<const context_type> context,
                               uintptr_t container_addr,
                               address_space_ref space, span<std::byte>,
                               size_t &out_count) noexcept {
    return context->bucket_count(space, container_addr, out_count);
  }

  static bool get_bucket_head(value_ref<const context_type> context,
                              uintptr_t container_addr,
                              address_space_ref space, span<std::byte>,
                              size_t bucket_index,
                              uintptr_t &out_node_addr) noexcept {
    uintptr_t buckets_address = 0;
    if (!context->buckets(space, container_addr, buckets_address))
      return false;
    const auto bucket = make_remote_offset_query<uintptr_t, RemotePtr>(
        static_cast<ptrdiff_t>(bucket_index * sizeof(RemotePtr)));
    return bucket(space, buckets_address, out_node_addr);
  }

  static bool get_next_node(value_ref<const context_type> context,
                            address_space_ref space, span<std::byte>,
                            uintptr_t node_addr,
                            uintptr_t &out_next_addr) noexcept {
    return context->next(space, node_addr, out_next_addr);
  }

  static bool format_entry(value_ref<const context_type> context,
                           address_space_ref space,
                           span<std::byte> scratch, uintptr_t node_addr,
                           const container_options &opts,
                           const sink &out) noexcept {
    if (opts.print_key) {
      const uintptr_t key_addr =
          detail::add_address_offset(node_addr, context->key_offset);
      if constexpr (remote_object_traits<Key>::is_registered) {
        remote_object_view view(key_addr, space, type_tag<Key>{}, scratch);
        formatter<remote_object_view>().format(view, out);
      } else {
        scratch_allocator allocator(scratch);
        Key *key = allocator.allocate<Key>();
        if (!key || !space.read_bytes(key_addr, key, sizeof(Key)))
          return false;
        formatter<Key>().format(*key, out);
      }
      out.write(opts.kv_separator);
    }

    if (opts.print_value) {
      const uintptr_t value_addr =
          detail::add_address_offset(node_addr, context->value_offset);
      if constexpr (remote_object_traits<Value>::is_registered) {
        remote_object_view view(value_addr, space, type_tag<Value>{}, scratch);
        formatter<remote_object_view>().format(view, out);
      } else {
        scratch_allocator allocator(scratch);
        Value *value = allocator.allocate<Value>();
        if (!value || !space.read_bytes(value_addr, value, sizeof(Value)))
          return false;
        formatter<Value>().format(*value, out);
      }
    }
    return true;
  }
};

/**
 * @brief Creates a chained-hash-table inspector for a conventional layout.
 */
template <typename Key, typename Value, typename RemotePtr = uintptr_t,
          typename RemoteSize = size_t>
[[nodiscard]] constexpr auto
make_remote_hash_table(uintptr_t container_addr,
                       ptrdiff_t buckets_ptr_offset,
                       ptrdiff_t bucket_count_offset,
                       ptrdiff_t node_next_offset, ptrdiff_t node_key_offset,
                       ptrdiff_t node_value_offset) noexcept {
  using tag =
      detail::hash_table_layout_tag<Key, Value, RemotePtr, RemoteSize>;
  using traits = remote_hash_table_traits<tag>;
  typename traits::context_type context{
      make_remote_offset_query<uintptr_t, RemotePtr>(buckets_ptr_offset),
      make_remote_offset_query<size_t, RemoteSize>(bucket_count_offset),
      make_remote_offset_query<uintptr_t, RemotePtr>(node_next_offset),
      node_key_offset,
      node_value_offset};
  return remote_hash_table<tag>(container_addr, std::move(context));
}

} // namespace microfmt
