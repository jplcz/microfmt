#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>
#include <microfmt/sink_provider.hpp>

namespace microfmt {
namespace testing {

TEST(SinkProviderTest, StatelessBackendDiscardsWrites) {
  sink_provider_ref ref{null_sink_provider_tag{}};

  EXPECT_TRUE(static_cast<bool>(ref));
  EXPECT_FALSE(ref.can_flush());

  ref.write("this goes nowhere");
  ref.flush(); // No-op; must not crash even though can_flush() is false.

  format_to(ref.as_sink(), "{}", 42); // Also must not crash.
}

TEST(SinkProviderTest, StatefulBackendWritesIntoSpan) {
  char buffer[16]{};
  span_sink_provider_context ctx{span<char>(buffer, sizeof(buffer)), 0};
  sink_provider_ref ref{span_sink_provider_tag{}, ctx};

  EXPECT_TRUE(static_cast<bool>(ref));
  EXPECT_FALSE(ref.can_flush()); // Traits do not define flush.

  ref.write("hello");
  EXPECT_EQ(ctx.pos, 5u);
  EXPECT_EQ(string_view(buffer, ctx.pos), "hello");

  ref.write(", world");
  EXPECT_EQ(ctx.pos, 12u);
  EXPECT_EQ(string_view(buffer, ctx.pos), "hello, world");
}

TEST(SinkProviderTest, StatefulBackendTruncatesOnOverflow) {
  char buffer[4]{};
  span_sink_provider_context ctx{span<char>(buffer, sizeof(buffer)), 0};
  sink_provider_ref ref{span_sink_provider_tag{}, ctx};

  ref.write("way too long for the buffer");
  EXPECT_EQ(ctx.pos, 4u);
  EXPECT_EQ(string_view(buffer, ctx.pos), "way ");

  // Buffer is now full: further writes must be safely discarded.
  ref.write("more");
  EXPECT_EQ(ctx.pos, 4u);
}

TEST(SinkProviderTest, AsSinkBridgesToFormatTo) {
  char buffer[64]{};
  span_sink_provider_context ctx{span<char>(buffer, sizeof(buffer)), 0};
  sink_provider_ref ref{span_sink_provider_tag{}, ctx};

  format_to(ref.as_sink(), "{} + {} = {}", 1, 2, 3);

  EXPECT_EQ(string_view(buffer, ctx.pos), "1 + 2 = 3");
}

TEST(SinkProviderTest, OwningWrapperExposesRefAndAsSink) {
  char buffer[32]{};
  sink_provider<span_sink_provider_tag> provider(
      span_sink_provider_context{span<char>(buffer, sizeof(buffer)), 0});

  provider.ref().write("via ref");
  format_to(provider.as_sink(), " and {}", "as_sink");

  // Both accessors operate on the same owned context.
  EXPECT_EQ(string_view(buffer, 19), "via ref and as_sink");
}

TEST(SinkProviderTest, DefaultConstructedRefIsEmpty) {
  sink_provider_ref ref;

  EXPECT_FALSE(static_cast<bool>(ref));
  EXPECT_FALSE(ref.can_flush());

  // Writing/flushing through an empty handle must be a safe no-op.
  ref.write("dropped");
  ref.flush();
}

} // namespace testing
} // namespace microfmt
