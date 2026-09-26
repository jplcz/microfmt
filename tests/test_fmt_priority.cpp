// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>

namespace {

// Has only reloco::Display -- no formatter<T>, no reloco::Debug.
struct display_only {
  int x;
};

// Has only reloco::Debug -- no formatter<T>, no reloco::Display.
struct debug_only {
  int x;
};

// Has both reloco::Display and reloco::Debug -- no formatter<T>.
struct display_and_debug {
  int x;
};

// Has a plain microfmt::formatter<T> AND both reloco traits, to prove
// formatter<T> wins for a plain placeholder while Debug<T> wins under
// as_debug().
struct all_three {
  int x;
};

// Has a plain microfmt::formatter<T> AND reloco::Display, but no
// reloco::Debug, to prove as_debug() prefers formatter<T> over Display<T>
// when Debug<T> is absent.
struct formatter_and_display {
  int x;
};

} // namespace

template <> struct reloco::Display<display_only> {
  static void format(const display_only &v, const reloco::sink &out) noexcept {
    out.write("display_only::display");
    (void)v;
  }
};

template <> struct reloco::Debug<debug_only> {
  static void format(const debug_only &v, const reloco::sink &out) noexcept {
    out.write("debug_only::debug");
    (void)v;
  }
};

template <> struct reloco::Display<display_and_debug> {
  static void format(const display_and_debug &v, const reloco::sink &out) noexcept {
    out.write("display_and_debug::display");
    (void)v;
  }
};

template <> struct reloco::Debug<display_and_debug> {
  static void format(const display_and_debug &v, const reloco::sink &out) noexcept {
    out.write("display_and_debug::debug");
    (void)v;
  }
};

template <> struct reloco::Display<all_three> {
  static void format(const all_three &v, const reloco::sink &out) noexcept {
    out.write("all_three::display");
    (void)v;
  }
};

template <> struct reloco::Debug<all_three> {
  static void format(const all_three &v, const reloco::sink &out) noexcept {
    out.write("all_three::debug");
    (void)v;
  }
};

template <> struct microfmt::formatter<all_three> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}
  void format(const all_three &v, const microfmt::sink &out) const noexcept {
    out.write("all_three::formatter");
    (void)v;
  }
};

template <> struct reloco::Display<formatter_and_display> {
  static void format(const formatter_and_display &v, const reloco::sink &out) noexcept {
    out.write("formatter_and_display::display");
    (void)v;
  }
};

template <> struct microfmt::formatter<formatter_and_display> {
  constexpr void parse(microfmt::format_parse_context &) noexcept {}
  void format(const formatter_and_display &v, const microfmt::sink &out) const noexcept {
    out.write("formatter_and_display::formatter");
    (void)v;
  }
};

TEST(FmtPriorityTest, PlainPlaceholderFallsBackToDisplayWhenNoFormatter) {
  EXPECT_EQ(microfmt::format<64>("{}", display_only{1}).view(), "display_only::display");
}

TEST(FmtPriorityTest, PlainPlaceholderFallsBackToDebugWhenNoFormatterOrDisplay) {
  EXPECT_EQ(microfmt::format<64>("{}", debug_only{1}).view(), "debug_only::debug");
}

TEST(FmtPriorityTest, PlainPlaceholderPrefersDisplayOverDebugWhenNoFormatter) {
  EXPECT_EQ(microfmt::format<64>("{}", display_and_debug{1}).view(), "display_and_debug::display");
}

TEST(FmtPriorityTest, PlainPlaceholderPrefersFormatterOverDisplayAndDebug) {
  EXPECT_EQ(microfmt::format<64>("{}", all_three{1}).view(), "all_three::formatter");
}

TEST(FmtPriorityTest, AsDebugPrefersDebugOverEverything) {
  all_three v{1};
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::as_debug(v)).view(), "all_three::debug");
}

TEST(FmtPriorityTest, AsDebugFallsBackToDisplayWhenOnlyDisplayExists) {
  display_only v{1};
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::as_debug(v)).view(), "display_only::display");
}

TEST(FmtPriorityTest, AsDebugPrefersFormatterOverDisplayWhenNoDebug) {
  formatter_and_display v{1};
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::as_debug(v)).view(), "formatter_and_display::formatter");
}

TEST(FmtPriorityTest, AsDebugUsesFormatterOverDisplayWhenBothExistButNoDebug) {
  // int has formatter<int> but neither reloco::Display<int> nor reloco::Debug<int>.
  int value = 42;
  EXPECT_EQ(microfmt::format<64>("{}", microfmt::as_debug(value)).view(), "42");
}
