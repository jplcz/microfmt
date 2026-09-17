// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "microfmt/inspector/address_space.hpp"
#include "microfmt/inspector/remote_forward_list.hpp"
#include "microfmt/sinks/stdio.hpp"
#include <cstddef>
#include <cstdint>

// Simulated node structure in remote memory: [ next_pointer, payload_value ]
struct ListNode {
  ListNode *next;
  int value;
};

// Simulated container structure pointing to the head node
struct ListContainer {
  ListNode *head;
};

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[1024];

  // Construct a mock remote linked list: [10] -> [20] -> [30] -> nullptr
  ListNode node3{nullptr, 30};
  ListNode node2{&node3, 20};
  ListNode node1{&node2, 10};
  ListContainer remote_list{&node1};

  uintptr_t list_addr = reinterpret_cast<uintptr_t>(&remote_list);

  auto list = microfmt::make_remote_forward_list<int>(
      list_addr, offsetof(ListContainer, head), offsetof(ListNode, next),
      offsetof(ListNode, value));
  auto container_view = list.view(
      space_ref, scratch,
      microfmt::container_options{.open_bracket = "[", .close_bracket = "]"});

  // Print the inspected linked list
  microfmt::print("Inspected Linked List: {}\n", container_view);
  // Output: Inspected Linked List: [10, 20, 30]

  return 0;
}
