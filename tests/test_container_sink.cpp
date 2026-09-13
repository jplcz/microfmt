#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include <microfmt/sinks/container_sink.hpp>

namespace {

class push_back_only_container {
public:
  void push_back(char c) { storage_.push_back(c); }

  [[nodiscard]] const char *data() const noexcept { return storage_.data(); }
  [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(storage_.data(), storage_.size());
  }

private:
  std::vector<char> storage_;
};

static_assert(microfmt::is_growable_char_container<std::string>,
              "std::string must be supported");
static_assert(microfmt::is_growable_char_container<std::vector<char>>,
              "std::vector<char> must be supported");
static_assert(microfmt::is_growable_char_container<push_back_only_container>,
              "push_back-only containers must be supported");
static_assert(!microfmt::is_growable_char_container<std::vector<int>>,
              "non-character containers must not be supported");

} // namespace

TEST(ContainerSinkTest, AppendsToStringAndVector) {
  std::string text = "prefix: ";
  std::vector<char> bytes;

  microfmt::format_to_container(text, "value={}", 42);
  microfmt::format_to_container(bytes, "{:04x}", 0x2a);

  EXPECT_EQ(text, "prefix: value=42");
  EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "002a");
}

TEST(ContainerSinkTest, UsesPushBackFallback) {
  push_back_only_container target;
  auto output = microfmt::make_container_sink(target);

  microfmt::format_to(output.as_sink(), "{} {}", "count", 7);

  EXPECT_EQ(target.view(), "count 7");
}

TEST(ContainerSinkTest, ReturnsRequestedContainerType) {
  const auto text = microfmt::format_as_container<>("id={}", 7);
  const auto bytes =
      microfmt::format_as_container<std::vector<char>>("id={}", 7);

  EXPECT_EQ(text, "id=7");
  EXPECT_EQ(std::string(bytes.begin(), bytes.end()), "id=7");
}
