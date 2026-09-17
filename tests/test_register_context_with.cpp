// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <microfmt/inspector/register_context.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace {

enum : uint32_t {
  reg_pc = 1,
  reg_sp = 2,
  reg_ssp = 3,
  reg_usp = 4,
  reg_alias_pc = 5,
  reg_fallback = 6,
  reg_frame_pointer = 7,
  reg_hardware_status = 8,
  reg_hardware_control = 9,
};

struct registers {
  uint32_t pc;
  struct {
    uint32_t child[2];
  } other;
};

using pc_field =
    microfmt::register_member_field<&registers::pc, reg_pc, reg_alias_pc>;

struct stack_fields {
  using state_type = registers;
  using value_type = uint32_t;

  [[nodiscard]] static constexpr bool matches(uint32_t index) noexcept {
    return index == reg_sp || index == reg_ssp || index == reg_usp;
  }

  [[nodiscard]] static constexpr const value_type *
  get(const state_type &state, uint32_t index) noexcept {
    return index == reg_usp ? &state.other.child[1] : &state.other.child[0];
  }

  [[nodiscard]] static constexpr value_type *
  get(state_type &state, uint32_t index) noexcept {
    return index == reg_usp ? &state.other.child[1] : &state.other.child[0];
  }
};

struct fallback_state {
  uint32_t value;
};

struct arm_registers {
  uint32_t cpsr;
  uint32_t r7;
  uint32_t r11;
};

struct select_arm_frame_pointer {
  template <typename Registers>
  [[nodiscard]] constexpr auto operator()(Registers &state,
                                          uint32_t) const noexcept
      -> decltype(&state.r7) {
    constexpr uint32_t thumb_bit = UINT32_C(1) << 5;
    return (state.cpsr & thumb_bit) != 0 ? &state.r7 : &state.r11;
  }
};

using arm_frame_pointer_field = microfmt::register_selected_field<
    arm_registers, uint32_t, select_arm_frame_pointer, reg_frame_pointer>;

struct hardware_registers {
  uint32_t status;
  uint32_t control;
  uint32_t reads;
};

bool read_hardware_register(const hardware_registers &state,
                            microfmt::address_space_ref, uint32_t index,
                            uint32_t &value) noexcept {
  if (index == reg_hardware_status) {
    value = state.status;
    return true;
  }
  if (index == reg_hardware_control) {
    value = state.control;
    return true;
  }
  return false;
}

bool write_hardware_register(hardware_registers &state,
                             microfmt::address_space_ref, uint32_t index,
                             const uint32_t &value) noexcept {
  if (index != reg_hardware_control)
    return false;
  state.control = value;
  return true;
}

using hardware_status_field = microfmt::register_callback_field<
    hardware_registers, uint32_t, read_hardware_register, nullptr,
    reg_hardware_status>;
using hardware_control_field = microfmt::register_callback_field<
    hardware_registers, uint32_t, read_hardware_register,
    write_hardware_register, reg_hardware_control>;

bool read_fallback(const void *opaque, microfmt::address_space_ref,
                   uint32_t index, void *output, size_t size) noexcept {
  if (index != reg_fallback || !output || size != sizeof(uint32_t))
    return false;
  const auto &state = *static_cast<const fallback_state *>(opaque);
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(output, &state.value, sizeof(state.value));
  MICROFMT_END_UNSAFE_BUFFER_USAGE;
  return true;
}

bool write_fallback(void *opaque, microfmt::address_space_ref, uint32_t index,
                    const void *input, size_t size) noexcept {
  if (index != reg_fallback || !input || size != sizeof(uint32_t))
    return false;
  auto &state = *static_cast<fallback_state *>(opaque);
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;
  std::memcpy(&state.value, input, sizeof(state.value));
  MICROFMT_END_UNSAFE_BUFFER_USAGE;
  return true;
}

TEST(RegisterContextRefWith, BindsMembersAliasesAndNestedSelections) {
  registers state{0x1000, {{0x2000, 0x3000}}};
  std::byte scratch[16]{};
  microfmt::register_context_ref_with<registers, pc_field, stack_fields>
      mapped(state, {}, scratch);
  microfmt::register_context_ref context = mapped.ref();

  uint32_t value = 0;
  EXPECT_TRUE(context.read(reg_pc, value));
  EXPECT_EQ(value, 0x1000U);
  EXPECT_TRUE(context.read(reg_alias_pc, value));
  EXPECT_EQ(value, 0x1000U);
  EXPECT_TRUE(context.read(reg_sp, value));
  EXPECT_EQ(value, 0x2000U);
  EXPECT_TRUE(context.read(reg_ssp, value));
  EXPECT_EQ(value, 0x2000U);
  EXPECT_TRUE(context.read(reg_usp, value));
  EXPECT_EQ(value, 0x3000U);

  const uint32_t new_usp = 0x4000;
  EXPECT_TRUE(context.write(reg_usp, new_usp));
  EXPECT_EQ(state.other.child[1], new_usp);
}

TEST(RegisterContextRefWith, RejectsWrongWidthsAndConstWrites) {
  const registers state{0x1000, {{0x2000, 0x3000}}};
  std::byte scratch[8]{};
  microfmt::register_context_ref_with<registers, pc_field, stack_fields>
      mapped(state, {}, scratch);
  auto context = mapped.ref();

  uint64_t wide = 0;
  EXPECT_FALSE(context.read(reg_pc, wide));
  const uint32_t replacement = 7;
  EXPECT_FALSE(context.write(reg_pc, replacement));
}

TEST(RegisterContextRefWith, ChainsToFallbackContext) {
  registers state{0x1000, {{0x2000, 0x3000}}};
  fallback_state fallback_state_value{0x5000};
  std::byte scratch[16]{};
  microfmt::register_context_ref fallback(
      &fallback_state_value, {read_fallback, write_fallback}, {}, scratch);
  microfmt::register_context_ref_with<registers, pc_field, stack_fields>
      mapped(state, {}, scratch, fallback);
  auto context = mapped.ref();

  uint32_t value = 0;
  EXPECT_TRUE(context.read(reg_fallback, value));
  EXPECT_EQ(value, 0x5000U);
  const uint32_t replacement = 0x6000;
  EXPECT_TRUE(context.write(reg_fallback, replacement));
  EXPECT_EQ(fallback_state_value.value, replacement);
  EXPECT_FALSE(context.read(99, value));
}

TEST(RegisterContextRefWith, ChainsMappedContextsInOrder) {
  registers lower_state{0x1111, {{0x2222, 0x3333}}};
  registers upper_state{0xaaaa, {{0xbbbb, 0xcccc}}};
  std::byte scratch[16]{};

  microfmt::register_context_ref_with<registers, stack_fields> lower(
      lower_state, {}, scratch);
  microfmt::register_context_ref_with<registers, pc_field> upper(
      upper_state, {}, scratch, lower.ref());
  auto context = upper.ref();

  uint32_t value = 0;
  EXPECT_TRUE(context.read(reg_pc, value));
  EXPECT_EQ(value, 0xaaaaU);
  EXPECT_TRUE(context.read(reg_sp, value));
  EXPECT_EQ(value, 0x2222U);
}

TEST(RegisterContextRefWith, SelectsFieldsFromSourceRegisterState) {
  constexpr uint32_t thumb_bit = UINT32_C(1) << 5;
  arm_registers state{thumb_bit, 0x7000, 0xb000};
  std::byte scratch[8]{};
  microfmt::register_context_ref_with<arm_registers,
                                      arm_frame_pointer_field>
      mapped(state, {}, scratch);
  auto context = mapped.ref();

  uint32_t value = 0;
  EXPECT_TRUE(context.read(reg_frame_pointer, value));
  EXPECT_EQ(value, 0x7000U);

  state.cpsr = 0;
  EXPECT_TRUE(context.read(reg_frame_pointer, value));
  EXPECT_EQ(value, 0xb000U);

  const uint32_t replacement = 0xc000;
  EXPECT_TRUE(context.write(reg_frame_pointer, replacement));
  EXPECT_EQ(state.r11, replacement);
  EXPECT_EQ(state.r7, 0x7000U);
}

TEST(RegisterContextRefWith, QueriesHardwareAndHonorsReadOnlyCallbacks) {
  hardware_registers state{0x1234, 0x5678, 0};
  std::byte scratch[8]{};
  microfmt::register_context_ref_with<hardware_registers,
                                      hardware_status_field,
                                      hardware_control_field>
      mapped(state, {}, scratch);
  auto context = mapped.ref();

  uint32_t value = 0;
  EXPECT_TRUE(context.read(reg_hardware_status, value));
  EXPECT_EQ(value, 0x1234U);
  EXPECT_TRUE(context.read(reg_hardware_control, value));
  EXPECT_EQ(value, 0x5678U);

  const uint32_t replacement = 0xabcd;
  EXPECT_FALSE(context.write(reg_hardware_status, replacement));
  EXPECT_EQ(state.status, 0x1234U);
  EXPECT_TRUE(context.write(reg_hardware_control, replacement));
  EXPECT_EQ(state.control, replacement);
}

} // namespace
