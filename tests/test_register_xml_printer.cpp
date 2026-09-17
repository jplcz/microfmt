// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/register_xml_printer.hpp>

namespace {

struct test_register_traits {
  inline static constexpr microfmt::array arch_layout{microfmt::gdb::register_mapping{"core", 0, 10, 32, "int32"}};
  inline static constexpr microfmt::array extended_arch_layout{
      microfmt::gdb::register_mapping{"vector", 1, 11, 128, ""}};
  inline static constexpr microfmt::array non_standard_arch_layout{
      microfmt::gdb::register_mapping{"control", 100, 12, 64, "int64"}};

  [[nodiscard]] static constexpr auto layout() noexcept {
    return microfmt::span<const microfmt::gdb::register_mapping>{arch_layout.data(), arch_layout.size()};
  }

  [[nodiscard]] static constexpr auto extended_layout() noexcept {
    return microfmt::span<const microfmt::gdb::register_mapping>{extended_arch_layout.data(),
                                                                 extended_arch_layout.size()};
  }

  [[nodiscard]] static constexpr auto non_standard_layout() noexcept {
    return microfmt::span<const microfmt::gdb::register_mapping>{non_standard_arch_layout.data(),
                                                                 non_standard_arch_layout.size()};
  }
};

struct test_abi_traits {
  using gdb_register_traits = test_register_traits;
};

TEST(RegisterXmlPrinter, FormatsTypedRegister) {
  constexpr microfmt::gdb::register_mapping mapping{"pc", 32, microfmt::dwarf::riscv::PC, 64, "code_ptr"};
  microfmt::buffer_sink<128> output;

  microfmt::gdb::register_xml_printer::format_register(output.as_sink(), mapping);

  EXPECT_EQ(output.view(), "<reg name=\"pc\" bitsize=\"64\" regnum=\"32\" "
                           "type=\"code_ptr\"/>");
}

TEST(RegisterXmlPrinter, OmitsEmptyRegisterType) {
  constexpr microfmt::gdb::register_mapping mapping{"custom", 7, 11, 32, ""};
  microfmt::buffer_sink<128> output;

  microfmt::gdb::register_xml_printer::format_register(output.as_sink(), mapping);

  EXPECT_EQ(output.view(), "<reg name=\"custom\" bitsize=\"32\" regnum=\"7\"/>");
}

TEST(RegisterXmlPrinter, FormatsCompleteTargetDocument) {
  microfmt::buffer_sink<1024> output;

  microfmt::gdb::register_xml_printer::format_target_xml<test_abi_traits>(output.as_sink(), "test");

  EXPECT_EQ(output.view(), "<?xml version=\"1.0\"?>\n"
                           "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                           "<target>\n"
                           "  <architecture>test</architecture>\n"
                           "  <feature name=\"org.gnu.gdb.custom\">\n"
                           "    <reg name=\"core\" bitsize=\"32\" regnum=\"0\" type=\"int32\"/>\n"
                           "    <reg name=\"vector\" bitsize=\"128\" regnum=\"1\"/>\n"
                           "    <reg name=\"control\" bitsize=\"64\" regnum=\"100\" "
                           "type=\"int64\"/>\n"
                           "  </feature>\n"
                           "</target>\n");
}

TEST(RegisterXmlPrinter, UsesCustomFeatureAndTruncatesToSinkCapacity) {
  microfmt::buffer_sink<256> custom_output;
  microfmt::gdb::register_xml_printer::format_target_xml<microfmt::riscv32_abi_traits>(
      custom_output.as_sink(), "riscv:rv32", "org.example.core");

  EXPECT_NE(custom_output.view().find("<architecture>riscv:rv32</architecture>"), microfmt::string_view::npos);
  EXPECT_NE(custom_output.view().find("<feature name=\"org.example.core\">"), microfmt::string_view::npos);

  microfmt::buffer_sink<16> truncated;
  microfmt::gdb::register_xml_printer::format_target_xml<microfmt::x86_64_abi_traits>(truncated.as_sink(),
                                                                                      "i386:x86-64");

  EXPECT_EQ(truncated.view().size(), 16U);
  EXPECT_EQ(truncated.view(), "<?xml version=\"1");
}

} // namespace
