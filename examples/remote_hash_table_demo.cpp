// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "microfmt/inspector/address_space.hpp"
#include "microfmt/inspector/remote_hash_table.hpp"
#include "microfmt/sinks/stdio.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

// Simulated hash node structure in remote memory: [ next_ptr, key, value ]
struct HashNode {
  HashNode *next;
  int key;
  int value;
};

// Simulated hash table container structure
struct RemoteHashMap {
  HashNode **buckets;
  size_t bucket_count;
};

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[1024];

  // Build a mock chaining hash table
  HashNode node2{nullptr, 2, 200};
  HashNode node1{&node2, 1, 100};

  HashNode *bucket_array[4] = {&node1, nullptr, nullptr,
                               nullptr}; // Bucket 0 has a chain of 2 nodes
  RemoteHashMap remote_map{bucket_array, 4};

  uintptr_t map_addr = reinterpret_cast<uintptr_t>(&remote_map);

  auto map = microfmt::make_remote_hash_table<int, int>(
      map_addr, offsetof(RemoteHashMap, buckets),
      offsetof(RemoteHashMap, bucket_count), offsetof(HashNode, next),
      offsetof(HashNode, key), offsetof(HashNode, value));

  // Wrap in remote_container_view with custom options
  microfmt::container_options map_opts{.kv_separator = " -> ",
                                       .entry_separator = ", ",
                                       .open_bracket = "{",
                                       .close_bracket = "}"};

  auto container_view = map.view(space_ref, scratch, map_opts);

  // Print inspected hash table
  microfmt::print("Inspected Hash Map: {}\n", container_view);
  // Output: Inspected Hash Map: {1 -> 100, 2 -> 200}

  return 0;
}