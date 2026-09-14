// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <microfmt/formatters/bitfield.hpp>
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/formatters/ranges.hpp>
#include <string_view>

// Register setup for bitfield probe
MICROFMT_DEFINE_REGISTER_TYPE(
    ProbeUartReg, uint32_t, MICROFMT_BIT_FLAG(0, "PE"),
    MICROFMT_BIT_FLAG(5, "RXNE"), MICROFMT_BIT_FLAG(7, "TXE"),
    MICROFMT_BIT_VALUE_DEC(0x3u << 10, 10, "DMA_BURST"),
    MICROFMT_BIT_VALUE_HEX(0xFu << 16, 16, "FIFO_CNT"))

namespace {

// Null sink to ensure operations are not optimized away while avoiding I/O
// overhead
inline void null_writer(void *, std::string_view) noexcept {}

// Helper macro to prevent inlining and force distinct stack frame generation
#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

// ============================================================================
// Probe Functions
// ============================================================================

NOINLINE uintptr_t probe_bitfield_stack(uint32_t val) {
  const uintptr_t frame_top =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

  microfmt::sink sink{nullptr, null_writer};
  ProbeUartReg reg{val};
  microfmt::format_to(sink, "Reg: {}", reg);

  const uintptr_t frame_bottom =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  return (frame_top >= frame_bottom) ? (frame_top - frame_bottom)
                                     : (frame_bottom - frame_top);
}

NOINLINE uintptr_t probe_fixed_point_stack(int32_t raw_val) {
  const uintptr_t frame_top =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

  microfmt::sink sink{nullptr, null_writer};
  auto fp_milli = microfmt::milli(raw_val);
  auto fp_micro = microfmt::micro(raw_val);
  microfmt::format_to(sink, "V: {} | I: {}", fp_milli, fp_micro);

  const uintptr_t frame_bottom =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  return (frame_top >= frame_bottom) ? (frame_top - frame_bottom)
                                     : (frame_bottom - frame_top);
}

NOINLINE uintptr_t probe_join_stack(const int32_t *arr, size_t len) {
  const uintptr_t frame_top =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

  microfmt::sink sink{nullptr, null_writer};
  microfmt::span<const int32_t> sp(arr, len);
  microfmt::format_to(sink, "Items: [{}]", microfmt::join(sp, ", "));

  const uintptr_t frame_bottom =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  return (frame_top >= frame_bottom) ? (frame_top - frame_bottom)
                                     : (frame_bottom - frame_top);
}

NOINLINE uintptr_t probe_escaped_stack(const uint8_t *data, size_t len) {
  const uintptr_t frame_top =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));

  microfmt::sink sink{nullptr, null_writer};
  microfmt::span<const uint8_t> sp(data, len);
  microfmt::format_to(sink, "Escaped: {}", microfmt::escaped(sp, true));

  const uintptr_t frame_bottom =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  return (frame_top >= frame_bottom) ? (frame_top - frame_bottom)
                                     : (frame_bottom - frame_top);
}

} // namespace

// ============================================================================
// Static Size & Object Layout Guarantees
// ============================================================================

TEST(StackProbeTest, ViewObjectSizes) {
  // 64-bit alignment: span (16B) + string_view (16B) + raw_value (8B padded) +
  // bool (8B padded) = 48B
  static_assert(sizeof(microfmt::bitfield_view) <= 48,
                "bitfield_view size exceeded limit");
  static_assert(sizeof(microfmt::fixed_point_view<1000, 3, int32_t>) <= 8,
                "fixed_point_view size exceeded limit");
  static_assert(sizeof(microfmt::escaped_view) <= 32,
                "escaped_view size exceeded limit");
  static_assert(sizeof(microfmt::join_view<const int *, const int *>) <= 32,
                "join_view size exceeded limit");

  SUCCEED();
}

// ============================================================================
// Runtime Stack Frame Headroom Probes
// ============================================================================

TEST(StackProbeTest, BitfieldStackUsage) {
  // Upper stack ceiling limit in unoptimized/debug builds: 512 bytes (typical
  // is < 128 bytes)
  constexpr size_t MAX_ALLOWABLE_STACK_BYTES = 512;

  uintptr_t used = probe_bitfield_stack(0x000408A1);
  EXPECT_LT(used, MAX_ALLOWABLE_STACK_BYTES);
}

TEST(StackProbeTest, FixedPointStackUsage) {
  constexpr size_t MAX_ALLOWABLE_STACK_BYTES = 512;

  uintptr_t used = probe_fixed_point_stack(-3295);
  EXPECT_LT(used, MAX_ALLOWABLE_STACK_BYTES);
}

TEST(StackProbeTest, JoinViewStackUsage) {
  constexpr size_t MAX_ALLOWABLE_STACK_BYTES = 512;

  const int32_t sample_data[] = {10, 20, 30, 40, 50, 60, 70, 80};
  uintptr_t used = probe_join_stack(sample_data, 8);
  EXPECT_LT(used, MAX_ALLOWABLE_STACK_BYTES);
}

TEST(StackProbeTest, EscapedViewStackUsage) {
  constexpr size_t MAX_ALLOWABLE_STACK_BYTES = 512;

  const uint8_t raw_payload[] = {0x00, 0x02, 'O', 'K', '\r', '\n', 0xFF};
  uintptr_t used = probe_escaped_stack(raw_payload, sizeof(raw_payload));
  EXPECT_LT(used, MAX_ALLOWABLE_STACK_BYTES);
}