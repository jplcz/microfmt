// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <microfmt/inspector/fp_unwinder.hpp>
#include <microfmt/inspector/remote_binary_tree.hpp>
#include <microfmt/inspector/remote_forward_list.hpp>
#include <microfmt/inspector/remote_hash_table.hpp>
#include <microfmt/inspector/remote_smart_ptr.hpp>
#include <microfmt/inspector/remote_vector.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

template <typename T> uintptr_t address_of(const T &object) noexcept {
  return reinterpret_cast<uintptr_t>(std::addressof(object));
}

microfmt::address_space_ref local_space() {
  return microfmt::address_space_ref(microfmt::local_space_tag{});
}

TEST(InspectorContainer, InvokesTypeErasedContextAndReportsFailures) {
  alignas(std::max_align_t) std::byte scratch[32]{};
  bool invoked = false;
  microfmt::container_options options;
  options.open_bracket = "[";
  options.close_bracket = "]";
  options.entry_separator = "|";

  auto context = microfmt::make_container_context(
      7, [&invoked](int &state, const microfmt::container_options &opts,
                    microfmt::address_space_ref, microfmt::span<std::byte>,
                    const microfmt::sink &out) noexcept {
        invoked = true;
        microfmt::format_to(out, "{}{}{}{}", opts.open_bracket, state,
                            opts.entry_separator, opts.close_bracket);
        return true;
      });
  int container = 0;
  microfmt::remote_container_view view(address_of(container), local_space(),
                                       scratch, &context, options);

  EXPECT_TRUE(view);
  EXPECT_FALSE(view.is_null());
  EXPECT_EQ(view.container_address(), address_of(container));
  EXPECT_EQ(view.options().entry_separator, "|");
  EXPECT_EQ(view.scratch().size(), sizeof(scratch));

  microfmt::buffer_sink<32> out;
  EXPECT_TRUE(view.format(out.as_sink()));
  EXPECT_TRUE(invoked);
  EXPECT_EQ(out.view(), "[7|]");
  EXPECT_EQ(microfmt::format<32>("{}", view).view(), "[7|]");

  auto failing_context = microfmt::make_container_context(
      0, [](int &, const microfmt::container_options &,
            microfmt::address_space_ref, microfmt::span<std::byte>,
            const microfmt::sink &) noexcept { return false; });
  microfmt::remote_container_view failing_view(
      address_of(container), local_space(), scratch, &failing_context);
  EXPECT_FALSE(failing_view.format(out.as_sink()));
  EXPECT_EQ(microfmt::format<32>("{}", failing_view).view(), "<fault>");

  microfmt::remote_container_view empty_view;
  EXPECT_FALSE(empty_view);
  EXPECT_TRUE(empty_view.is_null());
  EXPECT_EQ(microfmt::format<32>("{}", empty_view).view(), "<null>");
}

TEST(InspectorVector, RendersVectorAndCArrayLayoutsAndHandlesReadFaults) {
  struct vector_storage {
    uintptr_t data;
    size_t size;
    size_t capacity;
  };

  int elements[] = {10, 20, 30};
  vector_storage storage{address_of(elements), 3, 3};
  alignas(std::max_align_t) std::byte scratch[64]{};
  const auto space = local_space();
  auto vector_context = microfmt::remote_vector_traits::vector_layout<int>(
      offsetof(vector_storage, data), offsetof(vector_storage, size),
      offsetof(vector_storage, capacity))(address_of(storage));

  microfmt::container_options options;
  options.max_print = 2;
  microfmt::remote_container_view vector_view(address_of(storage), space,
                                              scratch, &vector_context, options);
  EXPECT_EQ(microfmt::format<64>("{}", vector_view).view(), "{10, 20, ...}");

  int carray[] = {4, 5};
  auto carray_context =
      microfmt::remote_vector_traits::carray_layout<int>(2)(address_of(carray));
  microfmt::container_options carray_options;
  carray_options.open_bracket = "[";
  carray_options.close_bracket = "]";
  microfmt::remote_container_view carray_view(address_of(carray), space,
                                              scratch, &carray_context,
                                              carray_options);
  EXPECT_EQ(microfmt::format<32>("{}", carray_view).view(), "[4, 5]");

  vector_storage unreadable_elements{0, 1, 1};
  auto fault_context = microfmt::remote_vector_traits::vector_layout<int>(
      offsetof(vector_storage, data), offsetof(vector_storage, size))(
      address_of(unreadable_elements));
  microfmt::remote_container_view fault_view(address_of(unreadable_elements),
                                             space, scratch, &fault_context);
  EXPECT_EQ(microfmt::format<32>("{}", fault_view).view(), "{<fault>}");
}

TEST(InspectorForwardList, TraversesNodesAndHandlesElementReadFaults) {
  struct node {
    uintptr_t next;
    int value;
  };
  struct list {
    uintptr_t head;
  };

  node second{0, 22};
  node first{address_of(second), 11};
  list storage{address_of(first)};
  alignas(std::max_align_t) std::byte scratch[64]{};
  auto context = microfmt::remote_forward_list_traits::forward_list_layout<int>(
      offsetof(list, head), offsetof(node, next), offsetof(node, value))(
      address_of(storage));
  microfmt::remote_container_view view(address_of(storage), local_space(),
                                       scratch, &context);
  EXPECT_EQ(microfmt::format<32>("{}", view).view(), "{11, 22}");

  alignas(std::max_align_t) std::byte too_small[1]{};
  auto fault_context =
      microfmt::remote_forward_list_traits::forward_list_layout<int>(
          offsetof(list, head), offsetof(node, next), offsetof(node, value))(
          address_of(storage));
  microfmt::remote_container_view fault_view(address_of(storage), local_space(),
                                             too_small, &fault_context);
  EXPECT_EQ(microfmt::format<32>("{}", fault_view).view(), "{<fault>}");
}

TEST(InspectorHashTable, TraversesBucketsAndCollisionChains) {
  struct node {
    uintptr_t next;
    int key;
    int value;
  };
  struct table {
    uintptr_t buckets;
    size_t bucket_count;
  };

  node collision_tail{0, 2, 20};
  node collision_head{address_of(collision_tail), 1, 10};
  node separate{0, 3, 30};
  uintptr_t buckets[] = {address_of(collision_head), address_of(separate)};
  table storage{address_of(buckets), 2};
  alignas(std::max_align_t) std::byte scratch[64]{};
  auto context = microfmt::remote_hash_table_traits::chaining_layout<int, int>(
      offsetof(table, buckets), offsetof(table, bucket_count),
      offsetof(node, next), offsetof(node, key), offsetof(node, value))(
      address_of(storage));
  microfmt::remote_container_view view(address_of(storage), local_space(),
                                       scratch, &context);

  EXPECT_EQ(microfmt::format<64>("{}", view).view(), "{1: 10, 2: 20, 3: 30}");
}

TEST(InspectorBinaryTree, RendersOrderedEntriesAndBoundsOrFaults) {
  struct node {
    uintptr_t left;
    uintptr_t right;
    int key;
    int value;
  };
  struct tree {
    uintptr_t root;
  };

  node left{0, 0, 1, 10};
  node right{0, 0, 3, 30};
  node root{address_of(left), address_of(right), 2, 20};
  tree storage{address_of(root)};
  alignas(std::max_align_t) std::byte scratch[128]{};
  auto layout = microfmt::remote_binary_tree_traits::bst_layout<int, int>(
      offsetof(tree, root), offsetof(node, left), offsetof(node, right),
      offsetof(node, key), offsetof(node, value));
  auto context = layout(address_of(storage));

  microfmt::container_options options;
  options.max_print = 2;
  microfmt::remote_container_view view(address_of(storage), local_space(),
                                       scratch, &context, options);
  EXPECT_EQ(microfmt::format<64>("{}", view).view(), "{1: 10, 2: 20, ...}");

  alignas(std::max_align_t) std::byte too_small[16]{};
  auto fault_context = layout(address_of(storage));
  microfmt::remote_container_view fault_view(address_of(storage), local_space(),
                                             too_small, &fault_context);
  EXPECT_EQ(microfmt::format<32>("{}", fault_view).view(), "{<fault>}");
}

TEST(InspectorSmartPointers, FormatsViewsAndPointerLayoutWrappers) {
  struct shared_storage {
    uintptr_t pointer;
    uintptr_t control_block;
  };
  struct control_block {
    int32_t use_count;
    int32_t weak_count;
  };

  int value = 42;
  uintptr_t unique_storage = address_of(value);
  shared_storage shared{address_of(value), 0};
  control_block control{3, 1};
  shared.control_block = address_of(control);
  uintptr_t intrusive_storage = address_of(value);
  alignas(std::max_align_t) std::byte scratch[64]{};
  const auto space = local_space();

  microfmt::remote_unique_ptr_view<int> unique(address_of(unique_storage),
                                                space, scratch);
  microfmt::remote_shared_ptr_view<int> shared_view(
      address_of(shared), space, scratch, offsetof(shared_storage, pointer),
      offsetof(shared_storage, control_block), offsetof(control_block, use_count),
      offsetof(control_block, weak_count));
  microfmt::remote_intrusive_ptr_view<int> intrusive(
      address_of(intrusive_storage), space, scratch, 0, 0);
  EXPECT_EQ(microfmt::format<32>("{}", unique).view(), "42");
  EXPECT_EQ(microfmt::format<64>("{}", shared_view).view(),
            "shared_ptr(42, use_count=3, weak_count=1)");
  EXPECT_EQ(microfmt::format<64>("{}", intrusive).view(),
            "intrusive_ptr(42, ref_count=42)");

  uintptr_t null_storage = 0;
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::remote_unique_ptr_view<int>(
                          address_of(null_storage), space, scratch))
                .view(),
            "nullptr");
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::remote_shared_ptr_view<int>(
                          address_of(null_storage), space, scratch, 0,
                          sizeof(uintptr_t), 0, sizeof(int32_t)))
                .view(),
            "shared_ptr(nullptr)");
  EXPECT_EQ(microfmt::format<32>(
                "{}", microfmt::remote_intrusive_ptr_view<int>(
                          address_of(null_storage), space, scratch, 0, 0))
                .view(),
            "intrusive_ptr(nullptr)");

  microfmt::remote_unique_ptr<int> unique_wrapper(address_of(value));
  microfmt::remote_intrusive_ptr<int> intrusive_wrapper(address_of(value));
  EXPECT_EQ(microfmt::format<32>("{}", unique_wrapper).view(),
            microfmt::format<32>("0x{:x}", address_of(value)).view());
  EXPECT_EQ(microfmt::format<32>("{}", intrusive_wrapper).view(),
            microfmt::format<32>("0x{:x}", address_of(value)).view());
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::remote_unique_ptr<int>{}).view(),
            "nullptr");
  EXPECT_EQ(microfmt::format<32>("{}", microfmt::remote_shared_ptr<int>{}).view(),
            "shared_ptr(nullptr)");
  EXPECT_EQ(
      microfmt::format<32>("{}", microfmt::remote_intrusive_ptr<int>{}).view(),
      "intrusive_ptr(nullptr)");
}

struct fake_fp_abi {
  using register_type = uintptr_t;
  static constexpr size_t pointer_size = sizeof(uintptr_t);
  static constexpr ptrdiff_t fp_slot_offset = 0;
  static constexpr ptrdiff_t ra_slot_offset =
      static_cast<ptrdiff_t>(sizeof(uintptr_t));

  static constexpr uintptr_t normalize_pc(uintptr_t raw_pc) noexcept {
    return raw_pc & ~static_cast<uintptr_t>(1);
  }
};

TEST(InspectorFrameUnwinder, StepsFrameRecordsAndRejectsInvalidRecords) {
  struct frame_record {
    uintptr_t saved_fp;
    uintptr_t saved_ra;
  };

  frame_record caller{0, 0};
  frame_record current{address_of(caller), 0x101};
  microfmt::fp_unwinder_context<fake_fp_abi> context{local_space()};
  uintptr_t next_fp = 0;
  uintptr_t next_pc = 0;

  using tag = microfmt::fp_unwinder_tag<fake_fp_abi>;
  EXPECT_TRUE(microfmt::frame_unwinder_traits<tag>::step(
      &context, address_of(current), next_fp, next_pc));
  EXPECT_EQ(next_fp, address_of(caller));
  EXPECT_EQ(next_pc, 0x100u);

  microfmt::frame_unwinder_ref unwinder(tag{}, context);
  EXPECT_TRUE(unwinder.step(address_of(current), next_fp, next_pc));
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      nullptr, address_of(current), next_fp, next_pc));
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(&context, 0, next_fp,
                                                           next_pc));
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, address_of(current) + 1, next_fp, next_pc));

  frame_record non_advancing{0, 0x100};
  non_advancing.saved_fp = address_of(non_advancing);
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, address_of(non_advancing), next_fp, next_pc));
  frame_record no_return_address{address_of(caller), 0};
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, address_of(no_return_address), next_fp, next_pc));
}

} // namespace
