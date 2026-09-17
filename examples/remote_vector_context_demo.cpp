// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/remote_vector.hpp>
#include <microfmt/sinks/stdio.hpp>
#include <vector>

// Simulated standard vector layout
struct MockVectorLayout {
  int *data;
  size_t size;
  size_t capacity;
};

int main() {
  auto space_ref =
      microfmt::address_space_ref::make<microfmt::local_space_tag>();
  std::byte scratch[1024];

  microfmt::container_options brackets_opts{.open_bracket = "[",
                                            .close_bracket = "]"};

  // =========================================================================
  // Test 1: vector_layout (std::vector-like structure)
  // =========================================================================
  {
    std::vector<int> vector_elements = {10, 20, 30, 40, 50};
    MockVectorLayout remote_vec{vector_elements.data(), vector_elements.size(),
                                vector_elements.capacity()};
    uintptr_t vec_addr = reinterpret_cast<uintptr_t>(&remote_vec);

    auto vector = microfmt::make_remote_vector<int>(
        vec_addr, offsetof(MockVectorLayout, data),
        offsetof(MockVectorLayout, size), offsetof(MockVectorLayout, capacity));
    auto vector_view = vector.view(space_ref, scratch, brackets_opts);

    // Print results
    microfmt::print("Tested vector_layout: {}\n", vector_view);
    // Expected Output: Tested vector_layout: [10, 20, 30, 40, 50]
  }

  // =========================================================================
  // Test 2: carray_layout (C-style array)
  // =========================================================================
  {
    int raw_c_array[4] = {100, 200, 300, 400};
    uintptr_t array_addr = reinterpret_cast<uintptr_t>(raw_c_array);
    size_t array_size = 4;

    auto carray =
        microfmt::make_remote_carray<int>(array_addr, array_size);
    auto carray_view = carray.view(space_ref, scratch, brackets_opts);

    // Print results
    microfmt::print("Tested carray_layout: {}\n", carray_view);
    // Expected Output: Tested carray_layout: [100, 200, 300, 400]
  }

  return 0;
}
