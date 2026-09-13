#include <array>
#include <gtest/gtest.h>
#include <microfmt/fmt.hpp>
#include <string_view>

TEST(FmtCompatTest, FormatToN) {
  char buffer[16];

  // Format with truncation (capacity = 6, total required = 13)
  auto res1 = fmt::format_to_n(buffer, 6, "Hello, {:s}!", "World");
  EXPECT_EQ(res1.size, 13u);
  EXPECT_EQ(static_cast<size_t>(res1.out - buffer), 6u);
  EXPECT_EQ(std::string_view(buffer, static_cast<size_t>(res1.out - buffer)),
            "Hello,");

  // Format with sufficient space (capacity = 16, total required = 7)
  auto res2 = fmt::format_to_n(buffer, sizeof(buffer), "Val: {:d}", 42);
  EXPECT_EQ(res2.size, 7u);
  EXPECT_EQ(static_cast<size_t>(res2.out - buffer), 7u);
  EXPECT_EQ(std::string_view(buffer, static_cast<size_t>(res2.out - buffer)),
            "Val: 42");

  // Dry run / count-only (n = 0, out = nullptr)
  auto res3 = fmt::format_to_n(nullptr, 0, "Test: {:d}", 12345);
  EXPECT_EQ(res3.size, 11u);
  EXPECT_EQ(res3.out, nullptr);
}

TEST(FmtCompatTest, FormatToRawPointer) {
  char buffer[32];
  char *end = fmt::format_to(buffer, "Temp: {:d} C", 25);
  *end = '\0';

  EXPECT_EQ(std::string_view(buffer), "Temp: 25 C");
}

TEST(FmtCompatTest, FormatStackReturn) {
  // Default capacity (128 bytes)
  auto res_default = fmt::format("Hex: 0x{:04x}", 0x2A);
  EXPECT_EQ(res_default.view(), "Hex: 0x002a");

  // Explicit capacity
  auto res_custom = fmt::format<64>("Status: {:s}", "OK");
  EXPECT_EQ(res_custom.view(), "Status: OK");
}

TEST(FmtCompatTest, JoinBridge) {
  const int vals[] = {1, 2, 3, 4};
  auto res = fmt::format("Nums: [{}]", fmt::join(vals, ", "));
  EXPECT_EQ(res.view(), "Nums: [1, 2, 3, 4]");
}
