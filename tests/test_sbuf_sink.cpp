#include <gtest/gtest.h>
#include <microfmt/sinks/sbuf_sink.hpp>

struct mock_sbuf {
  std::string content;
};

inline int sbuf_bcat(mock_sbuf *s, const void *buf, size_t len) {
  if (s && buf && len > 0) {
    s->content.append(static_cast<const char *>(buf), len);
  }
  return 0; // 0 typically means success in FreeBSD sbuf API
}

namespace microfmt {
namespace testing {

// ============================================================================
// Test Suite
// ============================================================================

TEST(SBufSinkTest, BasicFormatting) {
  mock_sbuf sb;

  // Instantiate the decoupled template with our mock type
  sbuf_sink<mock_sbuf> sink(&sb);

  // Format into the sbuf via the type-erased sink adapter
  format_to(sink.as_sink(), "Hello {}, the answer is {}", "sbuf", 42);

  // Verify that sbuf_bcat was correctly called and populated our mock
  EXPECT_EQ(sb.content, "Hello sbuf, the answer is 42");
}

TEST(SBufSinkTest, EmptyWritesAreIgnored) {
  mock_sbuf sb;
  sbuf_sink<mock_sbuf> sink(&sb);

  // Empty format string and empty arguments
  format_to(sink.as_sink(), "");

  EXPECT_TRUE(sb.content.empty());
}

TEST(SBufSinkTest, NativeHandleAccess) {
  mock_sbuf sb;
  sbuf_sink<mock_sbuf> sink(&sb);

  // Verify that we can retrieve the exact pointer back
  EXPECT_EQ(sink.native_handle(), &sb);
}

TEST(SBufSinkTest, BackInserterCompatibility) {
  mock_sbuf sb;
  sbuf_sink<mock_sbuf> sink(&sb);
  auto type_erased_sink = sink.as_sink();

  std::string_view sv = "Streamed directly";

  // Write byte-by-byte using the output iterator / sink semantics
  for (char c : sv) {
    type_erased_sink.push_back(c);
  }

  EXPECT_EQ(sb.content, "Streamed directly");
}

} // namespace testing
} // namespace microfmt