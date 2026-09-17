// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/register_xml_printer.hpp>

namespace {

TEST(RegisterXmlPrinter, FormatsTypedRegister) {
  constexpr microfmt::gdb::register_mapping mapping{
      "pc", 32, microfmt::dwarf::riscv::PC, 64, "code_ptr"};
  microfmt::buffer_sink<128> output;

  microfmt::gdb::register_xml_printer::format_register(output.as_sink(),
                                                       mapping);

  EXPECT_EQ(output.view(),
            "<reg name=\"pc\" bitsize=\"64\" regnum=\"32\" "
            "type=\"code_ptr\"/>");
}

TEST(RegisterXmlPrinter, OmitsEmptyRegisterType) {
  constexpr microfmt::gdb::register_mapping mapping{
      "custom", 7, 11, 32, ""};
  microfmt::buffer_sink<128> output;

  microfmt::gdb::register_xml_printer::format_register(output.as_sink(),
                                                       mapping);

  EXPECT_EQ(output.view(),
            "<reg name=\"custom\" bitsize=\"32\" regnum=\"7\"/>");
}

TEST(RegisterXmlPrinter, FormatsCompleteTargetDocument) {
  microfmt::buffer_sink<2048> output;

  microfmt::gdb::register_xml_printer::format_target_xml<
      microfmt::x86_abi_traits>(output.as_sink(), "i386");

  EXPECT_EQ(
      output.view(),
      "<?xml version=\"1.0\"?>\n"
      "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
      "<target>\n"
      "  <architecture>i386</architecture>\n"
      "  <feature name=\"org.gnu.gdb.custom\">\n"
      "    <reg name=\"eax\" bitsize=\"32\" regnum=\"0\" type=\"int32\"/>\n"
      "    <reg name=\"ecx\" bitsize=\"32\" regnum=\"1\" type=\"int32\"/>\n"
      "    <reg name=\"edx\" bitsize=\"32\" regnum=\"2\" type=\"int32\"/>\n"
      "    <reg name=\"ebx\" bitsize=\"32\" regnum=\"3\" type=\"int32\"/>\n"
      "    <reg name=\"esp\" bitsize=\"32\" regnum=\"4\" type=\"data_ptr\"/>\n"
      "    <reg name=\"ebp\" bitsize=\"32\" regnum=\"5\" type=\"data_ptr\"/>\n"
      "    <reg name=\"esi\" bitsize=\"32\" regnum=\"6\" type=\"int32\"/>\n"
      "    <reg name=\"edi\" bitsize=\"32\" regnum=\"7\" type=\"int32\"/>\n"
      "    <reg name=\"eip\" bitsize=\"32\" regnum=\"8\" type=\"code_ptr\"/>\n"
      "  </feature>\n"
      "</target>\n");
}

TEST(RegisterXmlPrinter, UsesCustomFeatureAndTruncatesToSinkCapacity) {
  microfmt::buffer_sink<256> custom_output;
  microfmt::gdb::register_xml_printer::format_target_xml<
      microfmt::riscv32_abi_traits>(
      custom_output.as_sink(), "riscv:rv32", "org.example.core");

  EXPECT_NE(custom_output.view().find(
                "<architecture>riscv:rv32</architecture>"),
            microfmt::string_view::npos);
  EXPECT_NE(custom_output.view().find(
                "<feature name=\"org.example.core\">"),
            microfmt::string_view::npos);

  microfmt::buffer_sink<16> truncated;
  microfmt::gdb::register_xml_printer::format_target_xml<
      microfmt::x86_64_abi_traits>(truncated.as_sink(), "i386:x86-64");

  EXPECT_EQ(truncated.view().size(), 16U);
  EXPECT_EQ(truncated.view(), "<?xml version=\"1");
}

} // namespace
