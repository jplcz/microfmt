// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <microfmt/inspector/fp_unwinder.hpp>
#include <microfmt/inspector/register_context.hpp>
#include <microfmt/inspector/register_view.hpp>
#include <microfmt/inspector/remote_binary_tree.hpp>
#include <microfmt/inspector/remote_forward_list.hpp>
#include <microfmt/inspector/remote_hash_table.hpp>
#include <microfmt/inspector/remote_smart_ptr.hpp>
#include <microfmt/inspector/remote_vector.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

template <typename T> uintptr_t address_of(const T &object) noexcept {
  return reinterpret_cast<uintptr_t>(std::addressof(object));
}

microfmt::address_space_ref local_space() {
  return microfmt::address_space_ref(microfmt::local_space_tag{});
}

struct fake_register_state {
  uint64_t value{0};
  uintptr_t probe_address{0};
  int expected_probe{0};
  bool read_succeeds{true};
  bool write_succeeds{true};
  mutable size_t read_calls{0};
  size_t write_calls{0};
  mutable uint32_t last_read_index{0};
  uint32_t last_write_index{0};
  mutable size_t last_read_size{0};
  size_t last_write_size{0};
  mutable bool read_space_forwarded{false};
  bool write_space_forwarded{false};
};

bool read_fake_register(const void *opaque_state,
                        microfmt::address_space_ref space,
                        uint32_t dwarf_reg_index, void *out_value,
                        size_t value_size) noexcept {
  const auto &state =
      *static_cast<const fake_register_state *>(opaque_state);
  ++state.read_calls;
  state.last_read_index = dwarf_reg_index;
  state.last_read_size = value_size;

  int probe = 0;
  state.read_space_forwarded =
      space.read(state.probe_address, probe) && probe == state.expected_probe;
  if (!state.read_succeeds || value_size > sizeof(state.value))
    return false;

  std::memcpy(out_value, &state.value, value_size);
  return true;
}

bool write_fake_register(void *opaque_state,
                         microfmt::address_space_ref space,
                         uint32_t dwarf_reg_index, const void *in_value,
                         size_t value_size) noexcept {
  auto &state = *static_cast<fake_register_state *>(opaque_state);
  ++state.write_calls;
  state.last_write_index = dwarf_reg_index;
  state.last_write_size = value_size;

  int probe = 0;
  state.write_space_forwarded =
      space.read(state.probe_address, probe) && probe == state.expected_probe;
  if (!state.write_succeeds || value_size > sizeof(state.value))
    return false;

  std::memcpy(&state.value, in_value, value_size);
  return true;
}

struct sparse_register_state {
  uint32_t index;
  uint64_t value;
  size_t value_size;
};

bool read_sparse_register(const void *opaque_state,
                          microfmt::address_space_ref, uint32_t dwarf_reg_index,
                          void *out_value, size_t value_size) noexcept {
  const auto &state =
      *static_cast<const sparse_register_state *>(opaque_state);
  if (dwarf_reg_index != state.index || value_size != state.value_size)
    return false;
  std::memcpy(out_value, &state.value, value_size);
  return true;
}

TEST(DwarfRegisterTraits, DefinesAllArchitectureRegisterCatalogs) {
  using arm_traits = microfmt::dwarf::arm32::register_traits;
  using aarch64_traits = microfmt::dwarf::aarch64::register_traits;
  using x86_traits = microfmt::dwarf::x86::register_traits;
  using x86_64_traits = microfmt::dwarf::x86_64::register_traits;
  using riscv_traits = microfmt::dwarf::riscv::register_traits;

  static_assert(arm_traits::gpr_registers.size() == 16);
  static_assert(aarch64_traits::gpr_registers.size() == 33);
  static_assert(x86_traits::gpr_registers.size() == 9);
  static_assert(x86_64_traits::gpr_registers.size() == 17);
  static_assert(riscv_traits::gpr_registers.size() == 33);
  static_assert(microfmt::dwarf::arm32::CNTFRQ ==
                microfmt::dwarf::generic_timer::CNTFRQ);
  static_assert(microfmt::dwarf::aarch64::CNTFRQ_EL0 ==
                microfmt::dwarf::generic_timer::CNTFRQ);
  static_assert(microfmt::dwarf::aarch64::CNTHVS_CVAL_EL2 ==
                microfmt::dwarf::generic_timer::CNTHVS_CVAL);

  EXPECT_EQ(arm_traits::gpr_registers.front().name, "R0");
  EXPECT_EQ(arm_traits::gpr_registers.back().name, "PC");
  EXPECT_EQ(aarch64_traits::gpr_registers.front().index,
            microfmt::dwarf::aarch64::X0);
  EXPECT_EQ(aarch64_traits::gpr_registers.back().index,
            microfmt::dwarf::aarch64::PC);
  EXPECT_GT(arm_traits::system_registers.size(), 40u);
  EXPECT_GT(aarch64_traits::system_registers.size(), 60u);
  EXPECT_EQ(x86_traits::system_registers.back().name, "CR4");
  EXPECT_EQ(x86_64_traits::system_registers.back().name, "DR7");
  EXPECT_EQ(riscv_traits::system_registers.front().name, "sstatus");
  EXPECT_EQ(riscv_traits::gpr_registers[28].name, "t3");
}

TEST(RegisterContextView, UsesArchitectureSystemRegisterTraits) {
  std::byte scratch[8]{};

  sparse_register_state arm_state{microfmt::dwarf::arm32::CNTVCT, 0x1234, 4};
  microfmt::register_context_ref arm_context(
      &arm_state, {&read_sparse_register, nullptr}, local_space(), scratch);
  EXPECT_EQ(
      microfmt::format<64>(
          "{}", microfmt::register_context_view<microfmt::arm_abi_traits>(
                    arm_context))
          .view(),
      "CNTVCT=0x00001234");

  sparse_register_state aarch64_state{
      microfmt::dwarf::aarch64::CNTHCTL_EL2, UINT64_C(0x1122334455667788), 8};
  microfmt::register_context_ref aarch64_context(
      &aarch64_state, {&read_sparse_register, nullptr}, local_space(),
      scratch);
  EXPECT_EQ(
      microfmt::format<64>(
          "{}",
          microfmt::register_context_view<microfmt::aarch64_abi_traits>(
              aarch64_context))
          .view(),
      "CNTHCTL_EL2=0x1122334455667788");

  sparse_register_state x86_state{microfmt::dwarf::x86::CR3, 0x12345000, 4};
  microfmt::register_context_ref x86_context(
      &x86_state, {&read_sparse_register, nullptr}, local_space(), scratch);
  EXPECT_EQ(
      microfmt::format<64>(
          "{}",
          microfmt::register_context_view<microfmt::x86_abi_traits>(
              x86_context))
          .view(),
      "CR3=0x12345000");

  sparse_register_state riscv_state{microfmt::dwarf::riscv::SATP,
                                    UINT64_C(0x8000000000012345), 8};
  microfmt::register_context_ref riscv_context(
      &riscv_state, {&read_sparse_register, nullptr}, local_space(), scratch);
  EXPECT_EQ(
      microfmt::format<64>(
          "{}",
          microfmt::register_context_view<microfmt::riscv64_abi_traits>(
              riscv_context))
          .view(),
      "satp=0x8000000000012345");
}

TEST(RegisterContextRef, ReportsNullAndSupportedOperations) {
  microfmt::register_context_ref empty;
  EXPECT_TRUE(empty.is_null());
  EXPECT_FALSE(empty);

  fake_register_state state;
  std::byte scratch[8]{};
  const auto space = local_space();

  microfmt::register_context_ref no_operations(
      &state, microfmt::register_context_vtable{}, space, scratch);
  EXPECT_TRUE(no_operations.is_null());
  EXPECT_FALSE(no_operations);

  microfmt::register_context_ref read_only(
      &state, {&read_fake_register, nullptr}, space, scratch);
  EXPECT_FALSE(read_only.is_null());
  EXPECT_TRUE(read_only);
  EXPECT_FALSE(read_only.write(1, uint32_t{42}));
  EXPECT_FALSE(read_only.write_raw(1, scratch, sizeof(scratch)));

  microfmt::register_context_ref write_only(
      &state, {nullptr, &write_fake_register}, space, scratch);
  EXPECT_FALSE(write_only.is_null());
  EXPECT_TRUE(write_only);
  uint32_t value = 0;
  EXPECT_FALSE(write_only.read(1, value));
  EXPECT_FALSE(write_only.read_raw(1, &value, sizeof(value)));

  microfmt::register_context_ref null_state(
      static_cast<fake_register_state *>(nullptr),
      {&read_fake_register, &write_fake_register}, space, scratch);
  EXPECT_TRUE(null_state.is_null());
  EXPECT_FALSE(null_state);
}

TEST(RegisterContextRef, DispatchesTypedAndRawReadsAndWrites) {
  int probe = 73;
  fake_register_state state;
  state.value = UINT64_C(0x1122334455667788);
  state.probe_address = address_of(probe);
  state.expected_probe = probe;
  std::byte scratch[16]{};
  const auto space = local_space();
  microfmt::register_context_ref context(
      &state, {&read_fake_register, &write_fake_register}, space, scratch);

  uint64_t typed_value = 0;
  EXPECT_TRUE(context.read(17, typed_value));
  EXPECT_EQ(typed_value, UINT64_C(0x1122334455667788));
  EXPECT_EQ(state.read_calls, 1u);
  EXPECT_EQ(state.last_read_index, 17u);
  EXPECT_EQ(state.last_read_size, sizeof(typed_value));
  EXPECT_TRUE(state.read_space_forwarded);

  const uint32_t replacement = UINT32_C(0xaabbccdd);
  EXPECT_TRUE(context.write(18, replacement));
  EXPECT_EQ(state.write_calls, 1u);
  EXPECT_EQ(state.last_write_index, 18u);
  EXPECT_EQ(state.last_write_size, sizeof(replacement));
  EXPECT_TRUE(state.write_space_forwarded);

  std::array<std::byte, sizeof(replacement)> raw_value{};
  EXPECT_TRUE(context.read_raw(19, raw_value.data(), raw_value.size()));
  EXPECT_EQ(std::memcmp(raw_value.data(), &replacement, sizeof(replacement)), 0);
  EXPECT_EQ(state.last_read_index, 19u);
  EXPECT_EQ(state.last_read_size, raw_value.size());

  const std::array<std::byte, 2> raw_replacement{std::byte{0x34},
                                                 std::byte{0x12}};
  EXPECT_TRUE(context.write_raw(20, raw_replacement.data(),
                                raw_replacement.size()));
  EXPECT_EQ(state.last_write_index, 20u);
  EXPECT_EQ(state.last_write_size, raw_replacement.size());
  EXPECT_EQ(state.value & UINT64_C(0xffff), UINT64_C(0x1234));

  EXPECT_TRUE(context.space());
  EXPECT_EQ(context.scratch().data(), scratch);
  EXPECT_EQ(context.scratch().size(), sizeof(scratch));
}

TEST(RegisterContextRef, PropagatesCallbackFailuresAndSupportsConstState) {
  int probe = 11;
  fake_register_state state;
  state.value = 42;
  state.probe_address = address_of(probe);
  state.expected_probe = probe;
  state.read_succeeds = false;
  state.write_succeeds = false;
  std::byte scratch[8]{};
  microfmt::register_context_ref context(
      &state, {&read_fake_register, &write_fake_register}, local_space(),
      scratch);

  uint64_t value = 0;
  EXPECT_FALSE(context.read(1, value));
  EXPECT_FALSE(context.read_raw(2, &value, sizeof(value)));
  EXPECT_FALSE(context.write(3, value));
  EXPECT_FALSE(context.write_raw(4, &value, sizeof(value)));
  EXPECT_EQ(state.read_calls, 2u);
  EXPECT_EQ(state.write_calls, 2u);

  const fake_register_state const_state{UINT64_C(0xfeedface),
                                        address_of(probe), probe};
  microfmt::register_context_ref const_context(
      &const_state, {&read_fake_register, nullptr}, local_space(), scratch);
  uint64_t const_value = 0;
  EXPECT_TRUE(const_context.read(5, const_value));
  EXPECT_EQ(const_value, UINT64_C(0xfeedface));
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
  static constexpr uint32_t fp_reg = 6;
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
  fake_register_state register_state;
  register_state.value = address_of(current);
  std::byte register_scratch[sizeof(uintptr_t)]{};
  microfmt::register_context_ref register_context(
      &register_state, {&read_fake_register, nullptr}, local_space(),
      register_scratch);
  uintptr_t next_fp = 0;
  uintptr_t next_pc = 0;

  using tag = microfmt::fp_unwinder_tag<fake_fp_abi>;
  EXPECT_TRUE(microfmt::frame_unwinder_traits<tag>::step(
      &context, register_context, next_fp, next_pc));
  EXPECT_EQ(next_fp, address_of(caller));
  EXPECT_EQ(next_pc, 0x100u);

  microfmt::frame_unwinder_ref unwinder(tag{}, context);
  EXPECT_TRUE(unwinder.step(register_context, next_fp, next_pc));
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      nullptr, register_context, next_fp, next_pc));
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, microfmt::register_context_ref{}, next_fp, next_pc));

  register_state.value = address_of(current) + 1;
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, register_context, next_fp, next_pc));

  frame_record non_advancing{0, 0x100};
  non_advancing.saved_fp = address_of(non_advancing);
  register_state.value = address_of(non_advancing);
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, register_context, next_fp, next_pc));
  frame_record no_return_address{address_of(caller), 0};
  register_state.value = address_of(no_return_address);
  EXPECT_FALSE(microfmt::frame_unwinder_traits<tag>::step(
      &context, register_context, next_fp, next_pc));
}

} // namespace
