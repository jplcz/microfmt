// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <microfmt/inspector/address_translator.hpp>
#include <microfmt/inspector/fp_unwinder.hpp>
#include <microfmt/inspector/memory_classifier.hpp>
#include <microfmt/inspector/memory_scanner.hpp>
#include <microfmt/inspector/register_context.hpp>
#include <microfmt/inspector/register_view.hpp>
#include <microfmt/inspector/remote_binary_tree.hpp>
#include <microfmt/inspector/remote_forward_list.hpp>
#include <microfmt/inspector/remote_hash_table.hpp>
#include <microfmt/inspector/remote_smart_ptr.hpp>
#include <microfmt/inspector/remote_vector.hpp>
#include <microfmt/inspector/symbol_resolver.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

struct stateless_translator_tag {};
struct stateful_translator_tag {};
struct stateless_classifier_tag {};
struct stateful_classifier_tag {};
struct scanner_space_tag {};
struct scanner_symbol_tag {};

struct translator_context {
  uintptr_t virtual_base;
  uintptr_t physical_base;
  size_t size;
  uint8_t space_id;
  mutable size_t calls{0};
  mutable uintptr_t last_virtual_address{0};
};

struct classifier_context {
  const microfmt::memory_region_info *regions;
  size_t region_count;
  mutable size_t calls{0};
  mutable uintptr_t last_virtual_address{0};
};

struct scanner_space_context {
  uintptr_t virtual_base;
  const uint8_t *data;
  size_t size;
  mutable size_t read_calls{0};
  mutable size_t largest_read{0};
  mutable uintptr_t last_read_address{0};
  const uint8_t *expected_read_buffer{nullptr};
  mutable bool used_external_read_buffer{true};
};

struct address_sequence {
  const uintptr_t *addresses;
  size_t count;
  mutable size_t index{0};
};

struct scanner_symbol_context {
  uintptr_t data_address;
  uintptr_t code_address;
  char *expected_scratch;
  size_t expected_scratch_size;
  microfmt::raw_resolved_symbol *expected_raw_symbol;
  mutable size_t calls{0};
  mutable bool used_external_scratch{true};
  mutable bool used_external_raw_symbol{true};
};

bool next_address(const void *opaque_context,
                  uintptr_t &out_address) noexcept {
  if (!opaque_context)
    return false;
  const auto &context = *static_cast<const address_sequence *>(opaque_context);
  if (context.index >= context.count)
    return false;
  out_address = context.addresses[context.index++];
  return true;
}

template <>
struct microfmt::address_translator_traits<stateless_translator_tag> {
  using context_type = void;

  static bool translate(const void *, uintptr_t virtual_address,
                        microfmt::translation_attributes &attributes) noexcept {
    if (virtual_address < 0x1000 || virtual_address >= 0x2000)
      return false;
    attributes = {.physical_address = virtual_address + 0x8000,
                  .space_id = 1,
                  .is_secure = false,
                  .readable = true,
                  .writable = false,
                  .executable = true,
                  .user_accessible = true};
    return true;
  }
};

template <>
struct microfmt::address_translator_traits<stateful_translator_tag> {
  using context_type = translator_context;

  static bool translate(const void *opaque_context, uintptr_t virtual_address,
                        microfmt::translation_attributes &attributes) noexcept {
    if (!opaque_context)
      return false;
    const auto &context =
        *static_cast<const translator_context *>(opaque_context);
    ++context.calls;
    context.last_virtual_address = virtual_address;

    if (virtual_address < context.virtual_base)
      return false;
    const uintptr_t offset = virtual_address - context.virtual_base;
    if (offset >= context.size)
      return false;

    attributes = {.physical_address = context.physical_base + offset,
                  .space_id = context.space_id,
                  .is_secure = true,
                  .readable = true,
                  .writable = true,
                  .executable = false,
                  .user_accessible = false};
    return true;
  }
};

template <>
struct microfmt::memory_classifier_traits<stateless_classifier_tag> {
  using context_type = void;

  static bool classify_address(const void *, uintptr_t virtual_address,
                               microfmt::memory_region_info &info) noexcept {
    if (virtual_address < 0x8000 || virtual_address >= 0x9000)
      return false;
    info = {.start_address = 0x8000,
            .end_address = 0x9000,
            .type = microfmt::memory_region_type::device_mmio,
            .space_id = 0,
            .readable = true,
            .writable = true,
            .executable = false};
    return true;
  }
};

template <>
struct microfmt::memory_classifier_traits<stateful_classifier_tag> {
  using context_type = classifier_context;

  static bool classify_address(const void *opaque_context,
                               uintptr_t virtual_address,
                               microfmt::memory_region_info &info) noexcept {
    if (!opaque_context)
      return false;
    const auto &context =
        *static_cast<const classifier_context *>(opaque_context);
    ++context.calls;
    context.last_virtual_address = virtual_address;

    for (size_t i = 0; i < context.region_count; ++i) {
      if (context.regions[i].contains(virtual_address)) {
        info = context.regions[i];
        return true;
      }
    }
    return false;
  }
};

template <> struct microfmt::address_space_traits<scanner_space_tag> {
  using context_type = scanner_space_context;

  static bool read_bytes(const void *opaque_context, uintptr_t address,
                         void *destination, size_t size) noexcept {
    if (!opaque_context || !destination)
      return false;
    const auto &context =
        *static_cast<const scanner_space_context *>(opaque_context);
    ++context.read_calls;
    if (size > context.largest_read)
      context.largest_read = size;
    context.last_read_address = address;
    if (context.expected_read_buffer) {
      context.used_external_read_buffer &=
          destination == context.expected_read_buffer;
    }

    if (size > 16 || address < context.virtual_base)
      return false;
    const uintptr_t offset = address - context.virtual_base;
    if (offset > context.size || size > context.size - offset)
      return false;
    std::memcpy(destination, context.data + offset, size);
    return true;
  }

  static bool read_string(const void *, uintptr_t, char *, size_t, size_t &,
                          bool &) noexcept {
    return false;
  }
};

template <> struct microfmt::symbol_resolver_traits<scanner_symbol_tag> {
  using context_type = scanner_symbol_context;

  static bool resolve(const void *opaque_context, uintptr_t address,
                      microfmt::span<char> scratch,
                      microfmt::raw_resolved_symbol &symbol) noexcept {
    if (!opaque_context)
      return false;
    const auto &context =
        *static_cast<const scanner_symbol_context *>(opaque_context);
    ++context.calls;
    context.used_external_scratch &=
        scratch.data() == context.expected_scratch &&
        scratch.size() == context.expected_scratch_size;
    context.used_external_raw_symbol &=
        &symbol == context.expected_raw_symbol;

    const char *name = nullptr;
    size_t name_size = 0;
    if (address == context.data_address) {
      name = "global_data";
      name_size = 11;
    } else if (address == context.code_address) {
      name = "kernel_entry";
      name_size = 12;
    } else {
      return false;
    }
    if (scratch.size() < name_size)
      return false;

    std::memcpy(scratch.data(), name, name_size);
    symbol.symbol_name = {scratch.data(), name_size};
    symbol.symbol_base = address;
    symbol.is_exact = true;
    return true;
  }
};

namespace {

template <typename T> uintptr_t address_of(const T &object) noexcept {
  return reinterpret_cast<uintptr_t>(std::addressof(object));
}

microfmt::address_space_ref local_space() {
  return microfmt::address_space_ref(microfmt::local_space_tag{});
}

TEST(AddressSpaceRef, ReturnsTypedReadErrorsAndValues) {
  microfmt::address_space_ref empty;
  uint32_t value = 0;

  auto invalid_handle = empty.read_bytes(0x1000, &value, sizeof(value));
  ASSERT_FALSE(invalid_handle);
  EXPECT_EQ(invalid_handle.error(),
            microfmt::address_space_error::invalid_handle);

  auto invalid_address = local_space().read_bytes(0, &value, sizeof(value));
  ASSERT_FALSE(invalid_address);
  EXPECT_EQ(invalid_address.error(),
            microfmt::address_space_error::invalid_address);

  auto invalid_buffer =
      local_space().read_bytes(address_of(value), nullptr, sizeof(value));
  ASSERT_FALSE(invalid_buffer);
  EXPECT_EQ(invalid_buffer.error(),
            microfmt::address_space_error::invalid_buffer);

  const uint32_t source = 0x12345678;
  auto loaded = local_space().read<uint32_t>(address_of(source));
  ASSERT_TRUE(loaded);
  EXPECT_EQ(*loaded, source);

  const uint8_t data[4]{};
  scanner_space_context context{
      .virtual_base = 0x1000, .data = data, .size = sizeof(data)};
  microfmt::address_space_ref scanner(scanner_space_tag{}, context);
  auto failed_read = scanner.read_bytes(0x2000, &value, sizeof(value));
  ASSERT_FALSE(failed_read);
  EXPECT_EQ(failed_read.error(), microfmt::address_space_error::read_failed);
}

TEST(AddressSpaceRef, ReturnsStringChunkMetadataAndErrors) {
  char source[] = "abc";
  char scratch[8]{};

  auto chunk = local_space().read_string_chunk(address_of(source), scratch);
  ASSERT_TRUE(chunk);
  EXPECT_EQ(chunk->length, 3U);
  EXPECT_TRUE(chunk->null_terminated);
  EXPECT_STREQ(scratch, source);

  auto empty_buffer =
      local_space().read_string_chunk(address_of(source), microfmt::span<char>{});
  ASSERT_FALSE(empty_buffer);
  EXPECT_EQ(empty_buffer.error(),
            microfmt::address_space_error::empty_buffer);
}

TEST(RemoteRef, ReturnsPreciseLoadErrors) {
  struct alignas(8) object {
    uint64_t value;
  };

  const object source{0x123456789abcdef0ULL};
  alignas(object) std::byte scratch[sizeof(object)]{};
  microfmt::remote_ref<object> valid(address_of(source), local_space(), scratch);

  auto loaded = valid.load();
  ASSERT_TRUE(loaded);
  EXPECT_EQ((*loaded)->value, source.value);

  microfmt::remote_ref<object> null_ref(0, local_space(), scratch);
  auto null_result = null_ref.load();
  ASSERT_FALSE(null_result);
  EXPECT_EQ(null_result.error(), microfmt::remote_load_error::null_address);

  std::byte small_scratch[sizeof(object) - 1]{};
  microfmt::remote_ref<object> too_small(address_of(source), local_space(),
                                         small_scratch);
  auto small_result = too_small.load();
  ASSERT_FALSE(small_result);
  EXPECT_EQ(small_result.error(),
            microfmt::remote_load_error::scratch_too_small);

  alignas(object) std::byte misaligned_storage[sizeof(object) + 1]{};
  microfmt::remote_ref<object> misaligned(
      address_of(source), local_space(),
      microfmt::span<std::byte>(misaligned_storage + 1, sizeof(object)));
  auto misaligned_result = misaligned.load();
  ASSERT_FALSE(misaligned_result);
  EXPECT_EQ(misaligned_result.error(),
            microfmt::remote_load_error::scratch_misaligned);

  microfmt::remote_ref<object> invalid_space(address_of(source), {}, scratch);
  auto invalid_space_result = invalid_space.load();
  ASSERT_FALSE(invalid_space_result);
  EXPECT_EQ(invalid_space_result.error(),
            microfmt::remote_load_error::invalid_address_space);
}

TEST(AddressTranslatorRef, ReportsAttributeValidity) {
  microfmt::translation_attributes attributes;
  EXPECT_FALSE(attributes.is_valid());

  attributes.readable = true;
  EXPECT_TRUE(attributes.is_valid());
  attributes.readable = false;
  attributes.writable = true;
  EXPECT_TRUE(attributes.is_valid());
  attributes.writable = false;
  attributes.executable = true;
  EXPECT_TRUE(attributes.is_valid());
}

TEST(AddressTranslatorRef, HandlesEmptyAndStatelessTranslators) {
  microfmt::address_translator_ref empty;
  EXPECT_FALSE(empty);

  microfmt::translation_attributes attributes{
      .physical_address = 0x55, .readable = true};
  EXPECT_FALSE(empty.translate(0x1000, attributes));
  EXPECT_EQ(attributes.physical_address, 0x55u);

  microfmt::address_translator_ref translator(stateless_translator_tag{});
  EXPECT_TRUE(translator);
  EXPECT_TRUE(translator.translate(0x1234, attributes));
  EXPECT_EQ(attributes.physical_address, 0x9234u);
  EXPECT_EQ(attributes.space_id, 1u);
  EXPECT_FALSE(attributes.is_secure);
  EXPECT_TRUE(attributes.readable);
  EXPECT_FALSE(attributes.writable);
  EXPECT_TRUE(attributes.executable);
  EXPECT_TRUE(attributes.user_accessible);

  auto made =
      microfmt::address_translator_ref::make<stateless_translator_tag>();
  attributes = {};
  EXPECT_TRUE(made.translate(0x1fff, attributes));
  EXPECT_EQ(attributes.physical_address, 0x9fffu);
  EXPECT_FALSE(made.translate(0x2000, attributes));
}

TEST(AddressTranslatorRef, ForwardsStateAndPropagatesFailures) {
  translator_context context{.virtual_base = 0x4000,
                             .physical_base = 0x100000,
                             .size = 0x2000,
                             .space_id = 7};
  microfmt::address_translator_ref translator(stateful_translator_tag{},
                                               context);
  EXPECT_TRUE(translator);

  microfmt::translation_attributes attributes;
  EXPECT_TRUE(translator.translate(0x5234, attributes));
  EXPECT_EQ(context.calls, 1u);
  EXPECT_EQ(context.last_virtual_address, 0x5234u);
  EXPECT_EQ(attributes.physical_address, 0x101234u);
  EXPECT_EQ(attributes.space_id, 7u);
  EXPECT_TRUE(attributes.is_secure);
  EXPECT_TRUE(attributes.readable);
  EXPECT_TRUE(attributes.writable);
  EXPECT_FALSE(attributes.executable);
  EXPECT_FALSE(attributes.user_accessible);

  attributes = {.physical_address = 0xabc};
  EXPECT_FALSE(translator.translate(0x3fff, attributes));
  EXPECT_EQ(attributes.physical_address, 0xabcu);
  EXPECT_FALSE(translator.translate(0x6000, attributes));
  EXPECT_EQ(context.calls, 3u);

  auto made =
      microfmt::address_translator_ref::make<stateful_translator_tag>(context);
  EXPECT_TRUE(made.translate(0x4000, attributes));
  EXPECT_EQ(attributes.physical_address, 0x100000u);
}

TEST(MemoryClassifierRef, ChecksHalfOpenRegionBounds) {
  constexpr microfmt::memory_region_info region{
      .start_address = 0x1000,
      .end_address = 0x2000,
      .type = microfmt::memory_region_type::kernel_data};

  static_assert(region.contains(0x1000));
  static_assert(region.contains(0x1fff));
  static_assert(!region.contains(0x0fff));
  static_assert(!region.contains(0x2000));
  EXPECT_TRUE(region.contains(0x1800));
}

TEST(MemoryClassifierRef, HandlesEmptyAndStatelessClassifiers) {
  microfmt::memory_classifier_ref empty;
  EXPECT_FALSE(empty);

  microfmt::memory_region_info info{
      .start_address = 1,
      .end_address = 2,
      .type = microfmt::memory_region_type::guard_page};
  EXPECT_FALSE(empty.classify_address(0x8000, info));
  EXPECT_EQ(info.type, microfmt::memory_region_type::guard_page);

  microfmt::memory_classifier_ref classifier(stateless_classifier_tag{});
  EXPECT_TRUE(classifier);
  EXPECT_TRUE(classifier.classify_address(0x8123, info));
  EXPECT_EQ(info.start_address, 0x8000u);
  EXPECT_EQ(info.end_address, 0x9000u);
  EXPECT_EQ(info.type, microfmt::memory_region_type::device_mmio);
  EXPECT_TRUE(info.readable);
  EXPECT_TRUE(info.writable);
  EXPECT_FALSE(info.executable);
  EXPECT_TRUE(info.contains(0x8fff));
  EXPECT_FALSE(info.contains(0x9000));

  auto made =
      microfmt::memory_classifier_ref::make<stateless_classifier_tag>();
  EXPECT_TRUE(made.classify_address(0x8fff, info));
  EXPECT_FALSE(made.classify_address(0x9000, info));
}

TEST(MemoryClassifierRef, ForwardsStateAndClassifiesConfiguredRegions) {
  constexpr microfmt::memory_region_info regions[]{
      {.start_address = 0x1000,
       .end_address = 0x2000,
       .type = microfmt::memory_region_type::user_code,
       .space_id = 42,
       .readable = true,
       .writable = false,
       .executable = true},
      {.start_address = 0x7000,
       .end_address = 0x8000,
       .type = microfmt::memory_region_type::process_stack,
       .space_id = 42,
       .readable = true,
       .writable = true,
       .executable = false}};
  classifier_context context{regions, sizeof(regions) / sizeof(regions[0])};
  microfmt::memory_classifier_ref classifier(stateful_classifier_tag{},
                                              context);
  EXPECT_TRUE(classifier);

  microfmt::memory_region_info info;
  EXPECT_TRUE(classifier.classify_address(0x7123, info));
  EXPECT_EQ(context.calls, 1u);
  EXPECT_EQ(context.last_virtual_address, 0x7123u);
  EXPECT_EQ(info.type, microfmt::memory_region_type::process_stack);
  EXPECT_EQ(info.space_id, 42u);
  EXPECT_TRUE(info.readable);
  EXPECT_TRUE(info.writable);
  EXPECT_FALSE(info.executable);

  info = {.start_address = 0xaaaa,
          .type = microfmt::memory_region_type::unknown};
  EXPECT_FALSE(classifier.classify_address(0x3000, info));
  EXPECT_EQ(info.start_address, 0xaaaau);
  EXPECT_EQ(context.calls, 2u);

  auto made =
      microfmt::memory_classifier_ref::make<stateful_classifier_tag>(context);
  EXPECT_TRUE(made.classify_address(0x1000, info));
  EXPECT_EQ(info.type, microfmt::memory_region_type::user_code);
  EXPECT_EQ(info.end_address, 0x2000u);
}

TEST(MemoryScanner, ScansRawAddressesAndSuppressesUnsafeRegions) {
  std::array<uint8_t, 256> data{};
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<uint8_t>(i);
  constexpr uintptr_t data_address = 0x100000;
  constexpr uintptr_t code_address = 0x200000;
  scanner_space_context space_context{
      .virtual_base = data_address, .data = data.data(), .size = data.size()};
  microfmt::address_space_ref space(scanner_space_tag{}, space_context);
  const microfmt::memory_region_info regions[]{
      {.start_address = data_address,
       .end_address = data_address + data.size(),
       .type = microfmt::memory_region_type::user_data,
       .space_id = 3,
       .readable = true,
       .writable = true,
       .executable = false},
      {.start_address = code_address,
       .end_address = code_address + 0x1000,
       .type = microfmt::memory_region_type::kernel_code,
       .space_id = 0,
       .readable = true,
       .writable = false,
       .executable = true}};
  classifier_context classifier_state{
      regions, sizeof(regions) / sizeof(regions[0])};
  microfmt::memory_classifier_ref classifier(stateful_classifier_tag{},
                                              classifier_state);
  const uintptr_t addresses[]{data_address, code_address, 0};
  microfmt::buffer_sink<1024> output;
  auto symbol_scratch = std::make_unique<char[]>(32);
  auto scanner_context = std::make_unique<microfmt::memory_scanner_context>();
  space_context.expected_read_buffer = scanner_context->dump_line_buffer;
  scanner_symbol_context symbol_context{
      .data_address = data_address,
      .code_address = code_address,
      .expected_scratch = symbol_scratch.get(),
      .expected_scratch_size = 32,
      .expected_raw_symbol = &scanner_context->raw_symbol};
  const auto resolver =
      microfmt::symbol_resolver_ref::make<scanner_symbol_tag>(symbol_context);
  scanner_context->options.dump_bytes = 96;
  scanner_context->options.symbol_resolver = resolver;
  scanner_context->symbol_scratch = {symbol_scratch.get(), 32};
  scanner_context->options.demangle_symbols = false;

  microfmt::memory_scanner<microfmt::x86_64_abi_traits>::scan_and_dump(
      space, classifier, microfmt::register_context_ref{}, addresses,
      *scanner_context, output.as_sink());

  const auto rendered = output.view();
  const auto data_type_position = rendered.find("type=user_data");
  const auto data_symbol_position = rendered.find("symbol=global_data");
  const auto data_dump_position = rendered.find("00 01 02 03");
  EXPECT_NE(data_type_position, microfmt::string_view::npos);
  EXPECT_NE(data_symbol_position, microfmt::string_view::npos);
  EXPECT_NE(data_dump_position, microfmt::string_view::npos);
  EXPECT_LT(data_type_position, data_symbol_position);
  EXPECT_LT(data_symbol_position, data_dump_position);
  EXPECT_NE(rendered.find("00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f "
                          " |................|"),
            microfmt::string_view::npos);
  EXPECT_NE(rendered.find("40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f "
                          " |@ABCDEFGHIJKLMNO|"),
            microfmt::string_view::npos);
  EXPECT_EQ(rendered.find("50 51 52 53"), microfmt::string_view::npos);
  EXPECT_NE(rendered.find("type=kernel_code"), microfmt::string_view::npos);
  EXPECT_NE(rendered.find("symbol=kernel_entry"), microfmt::string_view::npos);
  EXPECT_EQ(rendered.find("aa bb cc dd"), microfmt::string_view::npos);
  EXPECT_NE(rendered.find("type=unknown/unmapped"),
            microfmt::string_view::npos);
  EXPECT_EQ(symbol_context.calls, 2u);
  EXPECT_TRUE(symbol_context.used_external_scratch);
  EXPECT_TRUE(symbol_context.used_external_raw_symbol);
  EXPECT_EQ(scanner_context->region_info.type,
            microfmt::memory_region_type::unknown);
  EXPECT_EQ(space_context.read_calls, 5u);
  EXPECT_EQ(space_context.largest_read, 16u);
  EXPECT_EQ(space_context.last_read_address, data_address + 64);
  EXPECT_TRUE(space_context.used_external_read_buffer);
}

TEST(MemoryScanner, ScansOrderedAddressRegistersAndAbstractSources) {
  struct register_values {
    uintptr_t fp;
    uintptr_t x0;
    uintptr_t sp;
  } values{0x1111, 0x2222, 0x3333};
  const auto read_register =
      [](const void *opaque_state, microfmt::address_space_ref,
         uint32_t index, void *out_value, size_t value_size) noexcept {
        if (!opaque_state || value_size != sizeof(uintptr_t))
          return false;
        const auto &state =
            *static_cast<const register_values *>(opaque_state);
        const uintptr_t *value = nullptr;
        if (index == microfmt::dwarf::aarch64::FP)
          value = &state.fp;
        else if (index == microfmt::dwarf::aarch64::X0)
          value = &state.x0;
        else if (index == microfmt::dwarf::aarch64::SP)
          value = &state.sp;
        if (!value)
          return false;
        std::memcpy(out_value, value, value_size);
        return true;
      };
  std::byte scratch[sizeof(uintptr_t)]{};
  microfmt::register_context_ref registers(
      &values, {read_register, nullptr}, local_space(), scratch);
  microfmt::buffer_sink<512> register_output;
  auto scanner_context = std::make_unique<microfmt::memory_scanner_context>();
  scanner_context->options.dump_bytes = 0;

  microfmt::memory_scanner<microfmt::aarch64_abi_traits>::scan_and_dump(
      {}, {}, registers, {}, *scanner_context, register_output.as_sink());

  const auto register_text = register_output.view();
  EXPECT_NE(register_text.find("[FP] ->"), microfmt::string_view::npos);
  EXPECT_NE(register_text.find("[X0] ->"), microfmt::string_view::npos);
  EXPECT_LT(register_text.find("[FP] ->"), register_text.find("[X0] ->"));
  EXPECT_EQ(register_text.find("[SP] ->"), microfmt::string_view::npos);

  const uintptr_t source_addresses[]{0x4444, 0x5555};
  address_sequence sequence{source_addresses,
                            sizeof(source_addresses) /
                                sizeof(source_addresses[0])};
  microfmt::address_source_ref source(sequence, &next_address);
  microfmt::buffer_sink<512> source_output;
  microfmt::memory_scanner<microfmt::arm_abi_traits>::scan_and_dump(
      {}, {}, source, *scanner_context, source_output.as_sink());
  EXPECT_EQ(sequence.index, 2u);
  EXPECT_NE(source_output.view().find("addr=0x0000000000004444"),
            microfmt::string_view::npos);
  EXPECT_NE(source_output.view().find("addr=0x0000000000005555"),
            microfmt::string_view::npos);

  microfmt::address_source_ref empty_source;
  microfmt::buffer_sink<32> empty_output;
  microfmt::memory_scanner<microfmt::arm_abi_traits>::scan_and_dump(
      {}, {}, empty_source, *scanner_context, empty_output.as_sink());
  EXPECT_TRUE(empty_output.view().empty());
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

struct register_range_state {
  uint32_t last_index;
  uint64_t value;
  size_t value_size;
};

bool read_register_range(const void *opaque_state,
                         microfmt::address_space_ref, uint32_t dwarf_reg_index,
                         void *out_value, size_t value_size) noexcept {
  const auto &state =
      *static_cast<const register_range_state *>(opaque_state);
  if (dwarf_reg_index > state.last_index || value_size != state.value_size)
    return false;
  std::memcpy(out_value, &state.value, value_size);
  return true;
}

template <typename CandidateArray, typename RegisterArray>
constexpr bool address_candidates_are_unique_gprs(
    const CandidateArray &candidates, const RegisterArray &registers) noexcept {
  for (size_t i = 0; i < candidates.size(); ++i) {
    bool found = false;
    for (const auto &reg : registers) {
      if (reg.index == candidates[i].index && reg.name == candidates[i].name) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;

    for (size_t j = i + 1; j < candidates.size(); ++j) {
      if (candidates[i].index == candidates[j].index)
        return false;
    }
  }
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
  static_assert(static_cast<uint32_t>(microfmt::dwarf::arm32::CNTFRQ) ==
                static_cast<uint32_t>(
                    microfmt::dwarf::generic_timer::CNTFRQ));
  static_assert(static_cast<uint32_t>(
                    microfmt::dwarf::aarch64::CNTFRQ_EL0) ==
                static_cast<uint32_t>(
                    microfmt::dwarf::generic_timer::CNTFRQ));
  static_assert(static_cast<uint32_t>(
                    microfmt::dwarf::aarch64::CNTHVS_CVAL_EL2) ==
                static_cast<uint32_t>(
                    microfmt::dwarf::generic_timer::CNTHVS_CVAL));
  static_assert(address_candidates_are_unique_gprs(
      arm_traits::address_registers(), arm_traits::gpr_registers));
  static_assert(address_candidates_are_unique_gprs(
      aarch64_traits::address_registers(), aarch64_traits::gpr_registers));
  static_assert(address_candidates_are_unique_gprs(
      x86_traits::address_registers(), x86_traits::gpr_registers));
  static_assert(address_candidates_are_unique_gprs(
      x86_64_traits::address_registers(), x86_64_traits::gpr_registers));
  static_assert(address_candidates_are_unique_gprs(
      riscv_traits::address_registers(), riscv_traits::gpr_registers));

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

  constexpr auto arm_addresses = arm_traits::address_registers();
  constexpr auto aarch64_addresses = aarch64_traits::address_registers();
  constexpr auto x86_addresses = x86_traits::address_registers();
  constexpr auto x86_64_addresses = x86_64_traits::address_registers();
  constexpr auto riscv_addresses = riscv_traits::address_registers();
  EXPECT_EQ(arm_addresses[0].index, microfmt::dwarf::arm32::FP);
  EXPECT_EQ(arm_addresses[0].name, "FP");
  EXPECT_EQ(aarch64_addresses[0].index, microfmt::dwarf::aarch64::FP);
  EXPECT_EQ(x86_addresses[0].index, microfmt::dwarf::x86::FP);
  EXPECT_EQ(x86_64_addresses[0].index, microfmt::dwarf::x86_64::FP);
  EXPECT_EQ(riscv_addresses[0].index, microfmt::dwarf::riscv::FP);

  const auto contains = [](const auto &registers, uint32_t index) noexcept {
    for (const auto &candidate : registers) {
      if (candidate.index == index)
        return true;
    }
    return false;
  };
  EXPECT_FALSE(contains(arm_addresses, microfmt::dwarf::arm32::SP));
  EXPECT_FALSE(contains(arm_addresses, microfmt::dwarf::arm32::LR));
  EXPECT_FALSE(contains(arm_addresses, microfmt::dwarf::arm32::PC));
  EXPECT_FALSE(contains(aarch64_addresses, microfmt::dwarf::aarch64::SP));
  EXPECT_FALSE(contains(aarch64_addresses, microfmt::dwarf::aarch64::LR));
  EXPECT_FALSE(contains(aarch64_addresses, microfmt::dwarf::aarch64::PC));
  EXPECT_FALSE(contains(x86_addresses, microfmt::dwarf::x86::SP));
  EXPECT_FALSE(contains(x86_addresses, microfmt::dwarf::x86::PC));
  EXPECT_FALSE(contains(x86_64_addresses, microfmt::dwarf::x86_64::SP));
  EXPECT_FALSE(contains(x86_64_addresses, microfmt::dwarf::x86_64::PC));
  EXPECT_FALSE(contains(riscv_addresses, microfmt::dwarf::riscv::ZERO));
  EXPECT_FALSE(contains(riscv_addresses, microfmt::dwarf::riscv::SP));
  EXPECT_FALSE(contains(riscv_addresses, microfmt::dwarf::riscv::RA));
  EXPECT_FALSE(contains(riscv_addresses, microfmt::dwarf::riscv::PC));
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

TEST(RegisterContextView, GroupsRegistersByArchitectureWidth) {
  std::byte scratch[8]{};

  register_range_state arm_state{microfmt::dwarf::arm32::R3, 1, 4};
  microfmt::register_context_ref arm_context(
      &arm_state, {&read_register_range, nullptr}, local_space(), scratch);
  EXPECT_EQ(
      microfmt::format<128>(
          "{}", microfmt::register_context_view<microfmt::arm_abi_traits>(
                    arm_context))
          .view(),
      "R0=0x00000001  R1=0x00000001  R2=0x00000001\nR3=0x00000001");

  register_range_state aarch64_state{microfmt::dwarf::aarch64::X2, 1, 8};
  microfmt::register_context_ref aarch64_context(
      &aarch64_state, {&read_register_range, nullptr}, local_space(), scratch);
  EXPECT_EQ(
      microfmt::format<128>(
          "{}",
          microfmt::register_context_view<microfmt::aarch64_abi_traits>(
              aarch64_context))
          .view(),
      "X0=0x0000000000000001  X1=0x0000000000000001\n"
      "X2=0x0000000000000001");
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
  const auto context_scratch = context.scratch();
  EXPECT_EQ(context_scratch.data(), scratch);
  EXPECT_EQ(context_scratch.size(), sizeof(scratch));
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

  frame_record frames[2]{{0, 0}, {0, 0}};
  frame_record &current = frames[0];
  frame_record &caller = frames[1];
  current = {address_of(caller), 0x101};
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
