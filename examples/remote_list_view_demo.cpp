// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include "microfmt/inspector/remote_forward_list.hpp"
#include "microfmt/sinks/stdio.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

// Define the remote linked-list node structure and register its traits
struct RemoteProcessNode {
  uint32_t pid;
  int32_t state;
  microfmt::compat32_ptr<RemoteProcessNode> next;
};

MICROFMT_REMOTE_STRUCT_BEGIN(RemoteProcessNode)
MICROFMT_REMOTE_FIELD(pid, uint32_t)
MICROFMT_REMOTE_FIELD(state, int32_t)
MICROFMT_REMOTE_FIELD(next, microfmt::compat32_ptr<RemoteProcessNode>)
MICROFMT_REMOTE_STRUCT_END()

// Simple mock address space class
RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

class MockAddressSpace {
public:
  std::vector<std::byte> memory;

  bool read_bytes(uintptr_t addr, void *dest, size_t size) const noexcept {
    if (addr + size > memory.size())
      return false;
    std::memcpy(dest, memory.data() + addr, size);
    return true;
  }
};

// Define an address space tag and specialize address_space_traits
struct mock_space_tag {};

template <> struct microfmt::address_space_traits<mock_space_tag> {
  using context_type = MockAddressSpace;

  static bool read_bytes(microfmt::value_ref<const context_type> context,
                         uintptr_t addr, void *dest,
                         size_t size) noexcept {
    return context->read_bytes(addr, dest, size);
  }

  static bool read_string(microfmt::value_ref<const context_type>, uintptr_t,
                          char *, size_t, size_t &, bool &) noexcept {
    return false;
  }
};

int main() {
  MockAddressSpace target_space;
  target_space.memory.resize(16384, std::byte{0});

  uintptr_t node1_addr = 0x2000;
  uintptr_t node2_addr = 0x2040;
  uintptr_t node3_addr = 0x2080;

  // Populate node 3 (tail): PID 99, State 3, Next = nullptr
  RemoteProcessNode node3{99, 3,
                          microfmt::compat32_ptr<RemoteProcessNode>(nullptr)};
  std::memcpy(target_space.memory.data() + node3_addr, &node3, sizeof(node3));

  // Populate node 2: PID 42, State 2, Next -> node3_addr
  RemoteProcessNode node2{
      42, 2, microfmt::compat32_ptr<RemoteProcessNode>(uint32_t(node3_addr))};
  std::memcpy(target_space.memory.data() + node2_addr, &node2, sizeof(node2));

  // Populate node 1 (head): PID 1, State 1, Next -> node2_addr
  RemoteProcessNode node1{
      1, 1, microfmt::compat32_ptr<RemoteProcessNode>(uint32_t(node2_addr))};
  std::memcpy(target_space.memory.data() + node1_addr, &node1, sizeof(node1));
  constexpr uintptr_t list_addr = 0x100;
  const uint32_t head = static_cast<uint32_t>(node1_addr);
  std::memcpy(target_space.memory.data() + list_addr, &head, sizeof(head));

  std::byte scratch[1024];

  // Create type-erased address space handle bound to target_space
  microfmt::address_space_ref space_ref =
      microfmt::address_space_ref::make<mock_space_tag>(target_space);

  auto list = microfmt::make_remote_forward_list<RemoteProcessNode, uint32_t>(
      list_addr, 0, offsetof(RemoteProcessNode, next), 0);
  auto list_ref = list.view(space_ref, scratch);

  // Format and print the type-erased remote linked list chain cleanly
  microfmt::print("Type-Erased Remote Linked List:\n{}\n", list_ref);

  return 0;
}

RELOCO_END_UNSAFE_BUFFER_USAGE