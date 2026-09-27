// Compiled with MICROFMT_DISABLE_DEBUG_FLAG defined (see CMakeLists.txt), to
// verify that the macro degrades the implicit, format-string-driven `{:?}`
// flag to plain `{}` behavior for the affected types, while leaving
// reloco::Debug<T>/microfmt::as_debug() completely unaffected. See
// docs/porting.md, "Disabling runtime {:?} support to reduce code size".
#include <gtest/gtest.h>

#include <microfmt/formatters/std_debug.hpp>
#include <microfmt/microfmt.hpp>

#include <array>

#ifndef MICROFMT_DISABLE_DEBUG_FLAG
#error "This translation unit must be built with MICROFMT_DISABLE_DEBUG_FLAG defined."
#endif

namespace {

TEST(DisableDebugFlagTest, StringViewDoesNotQuoteOrEscape) {
  auto buffer =
      microfmt::format<64>("{:?}", microfmt::string_view("hi\"there\n"));
  EXPECT_EQ(buffer.view(), microfmt::string_view("hi\"there\n"));
}

TEST(DisableDebugFlagTest, StdStringViewDoesNotQuoteOrEscape) {
  auto buffer = microfmt::format<64>("{:?}", std::string_view("hi\"there"));
  EXPECT_EQ(buffer.view(), microfmt::string_view("hi\"there"));
}

TEST(DisableDebugFlagTest, ConstCharPointerDoesNotQuoteOrEscape) {
  auto buffer = microfmt::format<64>("{:?}", "hi\"there");
  EXPECT_EQ(buffer.view(), microfmt::string_view("hi\"there"));
}

TEST(DisableDebugFlagTest, CharDoesNotQuoteOrEscape) {
  auto buffer = microfmt::format<64>("{:?}", '\'');
  EXPECT_EQ(buffer.view(), microfmt::string_view("'"));
}

TEST(DisableDebugFlagTest, AsDebugStillWorksForBuiltins) {
  int value = 42;
  auto buffer = microfmt::format<64>("{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), microfmt::string_view("42"));
}

TEST(DisableDebugFlagTest, AsDebugStillUsesDebugTSpecialization) {
  // as_debug() never goes through consume_debug_flag() at all -- it always
  // prefers a type's Debug<T> specialization when one exists, entirely
  // independent of MICROFMT_DISABLE_DEBUG_FLAG. std::array has no
  // formatter<T>/Display<T> of its own, so this only compiles/succeeds if
  // Debug<std::array<T, N>> (from std_debug.hpp) is actually being used.
  std::array<int, 3> value{1, 2, 3};
  auto buffer = microfmt::format<64>("{}", microfmt::as_debug(value));
  EXPECT_EQ(buffer.view(), microfmt::string_view("[1, 2, 3]"));
}

} // namespace
