// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/gdb_register_array.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {

constexpr std::size_t max_registers = 128;
constexpr std::size_t max_register_bytes = 80;

struct register_state {
  std::array<std::array<uint8_t, max_register_bytes>, max_registers> values{};
  std::array<std::size_t, max_registers> sizes{};
  uint32_t read_failure{std::numeric_limits<uint32_t>::max()};
  uint32_t write_failure{std::numeric_limits<uint32_t>::max()};
  std::size_t write_count{0};
};

MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE

bool read_register(const void *opaque_state, microfmt::address_space_ref,
                   uint32_t dwarf_index, void *out_value,
                   std::size_t value_size) noexcept {
  const auto &state = *static_cast<const register_state *>(opaque_state);
  if (dwarf_index >= state.values.size() ||
      dwarf_index == state.read_failure ||
      state.sizes[dwarf_index] != value_size) {
    return false;
  }
  std::memcpy(out_value, state.values[dwarf_index].data(), value_size);
  return true;
}

bool write_register(void *opaque_state, microfmt::address_space_ref,
                    uint32_t dwarf_index, const void *value,
                    std::size_t value_size) noexcept {
  auto &state = *static_cast<register_state *>(opaque_state);
  if (dwarf_index >= state.values.size() ||
      dwarf_index == state.write_failure ||
      value_size > state.values[dwarf_index].size()) {
    return false;
  }
  std::memcpy(state.values[dwarf_index].data(), value, value_size);
  state.sizes[dwarf_index] = value_size;
  ++state.write_count;
  return true;
}

MICROFMT_END_UNSAFE_BUFFER_USAGE

microfmt::register_context_ref make_context(
    register_state &state, microfmt::span<std::byte> scratch = {}) {
  return {&state, {read_register, write_register}, {}, scratch};
}

TEST(GdbRegisterArray, EncodesAndDecodesSingleRegister) {
  register_state source;
  source.values[microfmt::dwarf::x86_64::rax][0] = 0x01;
  source.values[microfmt::dwarf::x86_64::rax][1] = 0x23;
  source.values[microfmt::dwarf::x86_64::rax][2] = 0xab;
  source.values[microfmt::dwarf::x86_64::rax][3] = 0xcd;
  source.values[microfmt::dwarf::x86_64::rax][4] = 0xef;
  source.values[microfmt::dwarf::x86_64::rax][5] = 0x45;
  source.values[microfmt::dwarf::x86_64::rax][6] = 0x67;
  source.values[microfmt::dwarf::x86_64::rax][7] = 0x89;
  source.sizes[microfmt::dwarf::x86_64::rax] = 8;
  auto source_context = make_context(source);
  const auto *mapping =
      microfmt::gdb::register_traits<microfmt::gdb::tags::x86_64>::
          find_by_name("rax");
  ASSERT_NE(mapping, nullptr);

  microfmt::buffer_sink<32> encoded;
  microfmt::gdb::register_array_encoder::encode_single_register(
      encoded.as_sink(), source_context, *mapping);
  EXPECT_EQ(encoded.view(), "0123abcdef456789");

  register_state destination;
  auto destination_context = make_context(destination);
  EXPECT_TRUE(microfmt::gdb::register_array_decoder::decode_single_register(
      "0123ABCDEF456789", destination_context, *mapping));
  EXPECT_EQ(destination.write_count, 1U);
  EXPECT_EQ(destination.sizes[microfmt::dwarf::x86_64::rax], 8U);
  EXPECT_EQ(destination.values[microfmt::dwarf::x86_64::rax],
            source.values[microfmt::dwarf::x86_64::rax]);
}

TEST(GdbRegisterArray, RoundTripsCompleteX86Layout) {
  using traits = microfmt::gdb::register_traits<microfmt::gdb::tags::x86>;

  register_state source;
  for (const auto &mapping : traits::layout()) {
    source.sizes[mapping.dwarf_index] = mapping.bit_size / 8;
    for (std::size_t i = 0; i < source.sizes[mapping.dwarf_index]; ++i) {
      source.values[mapping.dwarf_index][i] =
          static_cast<uint8_t>(mapping.gdb_index * 16 + i);
    }
  }

  microfmt::buffer_sink<256> encoded;
  auto source_context = make_context(source);
  microfmt::gdb::register_array_encoder::encode_all_registers<
      microfmt::x86_abi_traits>(encoded.as_sink(), source_context);
  EXPECT_EQ(encoded.view().size(), traits::layout().size() * 8);

  register_state destination;
  auto destination_context = make_context(destination);
  EXPECT_TRUE(microfmt::gdb::register_array_decoder::decode_all_registers<
              microfmt::x86_abi_traits>(encoded.view(), destination_context));
  EXPECT_EQ(destination.write_count, traits::layout().size());

  for (const auto &mapping : traits::layout()) {
    EXPECT_EQ(destination.values[mapping.dwarf_index],
              source.values[mapping.dwarf_index]);
  }
}

TEST(GdbRegisterArray, HandlesUnavailableAndTruncatedValues) {
  constexpr microfmt::gdb::register_mapping mapping{
      "test", 0, 7, 32, "int32"};
  register_state state;
  state.sizes[mapping.dwarf_index] = 4;
  state.read_failure = mapping.dwarf_index;
  auto context = make_context(state);
  microfmt::buffer_sink<16> encoded;

  microfmt::gdb::register_array_encoder::encode_single_register(
      encoded.as_sink(), context, mapping);
  EXPECT_EQ(encoded.view(), "xxxxxxxx");

  EXPECT_TRUE(microfmt::gdb::register_array_decoder::decode_single_register(
      "xxxxxxxx", context, mapping));
  EXPECT_EQ(state.write_count, 0U);
  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_single_register(
      "0011", context, mapping));

  register_state truncated;
  auto truncated_context = make_context(truncated);
  EXPECT_TRUE(microfmt::gdb::register_array_decoder::decode_all_registers<
              microfmt::x86_abi_traits>("01020304", truncated_context));
  EXPECT_EQ(truncated.write_count, 1U);
}

TEST(GdbRegisterArray, RejectsMalformedInputAndWriteFailures) {
  constexpr microfmt::gdb::register_mapping mapping{
      "test", 0, 9, 16, "int16"};
  register_state state;
  state.write_failure = mapping.dwarf_index;
  auto context = make_context(state);

  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_single_register(
      "00q1", context, mapping));
  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_single_register(
      "0011", context, mapping));
  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_single_register(
      "0011", microfmt::register_context_ref{}, mapping));
  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_all_registers<
               microfmt::x86_abi_traits>("00112233",
                                         microfmt::register_context_ref{}));
}

TEST(GdbRegisterArray, UsesContextScratchForLargeRegisters) {
  constexpr std::size_t byte_size = 65;
  constexpr microfmt::gdb::register_mapping mapping{
      "vector", 0, 42, static_cast<uint32_t>(byte_size * 8), "vector"};
  register_state source;
  source.sizes[mapping.dwarf_index] = byte_size;
  for (std::size_t i = 0; i < byte_size; ++i) {
    source.values[mapping.dwarf_index][i] = static_cast<uint8_t>(i);
  }
  std::byte source_scratch[byte_size]{};
  auto source_context = make_context(source, source_scratch);
  microfmt::buffer_sink<byte_size * 2> encoded;

  microfmt::gdb::register_array_encoder::encode_single_register(
      encoded.as_sink(), source_context, mapping);
  EXPECT_EQ(encoded.view().size(), byte_size * 2);
  EXPECT_TRUE(encoded.view().starts_with("00010203"));
  EXPECT_EQ(encoded.view().substr(encoded.view().size() - 8), "3d3e3f40");

  register_state destination;
  std::byte destination_scratch[byte_size]{};
  auto destination_context =
      make_context(destination, destination_scratch);
  EXPECT_TRUE(microfmt::gdb::register_array_decoder::decode_single_register(
      encoded.view(), destination_context, mapping));
  EXPECT_EQ(destination.values[mapping.dwarf_index],
            source.values[mapping.dwarf_index]);

  register_state no_scratch;
  auto no_scratch_context = make_context(no_scratch);
  microfmt::buffer_sink<byte_size * 2> unavailable;
  microfmt::gdb::register_array_encoder::encode_single_register(
      unavailable.as_sink(), no_scratch_context, mapping);
  EXPECT_EQ(unavailable.view().size(), byte_size * 2);
  for (char ch : unavailable.view()) {
    EXPECT_EQ(ch, 'x');
  }
  EXPECT_FALSE(microfmt::gdb::register_array_decoder::decode_single_register(
      encoded.view(), no_scratch_context, mapping));
}

} // namespace
