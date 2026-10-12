// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <chrono>
#include <gtest/gtest.h>
#include <microfmt/log/extended_logger.hpp>
#include <microfmt/log/extended_sink.hpp>
#include <thread>

namespace {

using namespace microfmt::log;

// Bounded record store with its own lock. A full store overwrites its oldest
// record only for contexts that cannot wait.
struct bounded_store_tag {};

class bounded_store {
public:
  static constexpr std::size_t capacity = 2;

  void push(const extended_log_msg &msg) noexcept {
    mutex_.lock();
    if (count_ == capacity) {
      if (msg.meta.can_wait()) {
        mutex_.unlock();
        return;
      }
      head_ = (head_ + 1) % capacity;
      --count_;
      ++overwritten_;
    }
    tids_[(head_ + count_) % capacity] = msg.meta.tid;
    ++count_;
    mutex_.unlock();
  }

  [[nodiscard]] bool is_full() noexcept {
    mutex_.lock();
    const bool r = count_ == capacity;
    mutex_.unlock();
    return r;
  }

  // Reader side: takes only the sink's own lock; the caller notifies afterwards.
  bool pop(std::uint32_t &tid) noexcept {
    mutex_.lock();
    const bool ok = count_ != 0;
    if (ok) {
      tid = tids_[head_];
      head_ = (head_ + 1) % capacity;
      --count_;
    }
    mutex_.unlock();
    return ok;
  }

  std::size_t overwritten_{0};

private:
  reloco::mutex mutex_{};
  reloco::array<std::uint32_t, capacity> tids_{};
  std::size_t head_{0};
  std::size_t count_{0};
};

struct no_full_tag {};
struct counter {
  int n{0};
};

} // namespace

template <> struct microfmt::log::extended_log_sink_traits<bounded_store_tag> {
  using context_type = bounded_store;
  static void log(microfmt::value_ref<bounded_store> ctx, const extended_log_msg &msg) noexcept { ctx->push(msg); }
  static bool full(microfmt::value_ref<bounded_store> ctx, const extended_log_msg &) noexcept { return ctx->is_full(); }
};

template <> struct microfmt::log::extended_log_sink_traits<no_full_tag> {
  using context_type = counter;
  static void log(microfmt::value_ref<counter> ctx, const extended_log_msg &) noexcept { ++ctx->n; }
};

namespace {

log_meta fake_context(void *user, const log_msg &) noexcept {
  log_meta m;
  m.pid = 7;
  m.tid = 42;
  m.cpu = 3;
  m.set(log_flag::can_wait, *static_cast<bool *>(user));
  m.set_user_flags(0x5);
  return m;
}

class ExtendedLogTest : public ::testing::Test {
protected:
  bounded_store store;
  counter cnt;
};

} // namespace

TEST_F(ExtendedLogTest, MetaFlagsRoundTrip) {
  log_meta m;
  EXPECT_FALSE(m.can_wait());
  EXPECT_EQ(m.cpu, no_cpu);
  m.set(log_flag::can_wait);
  m.set_user_flags(0xABCD);
  EXPECT_TRUE(m.can_wait());
  EXPECT_EQ(m.user_flags(), 0xABCDU);
  m.set(log_flag::can_wait, false);
  EXPECT_FALSE(m.can_wait());
  EXPECT_EQ(m.user_flags(), 0xABCDU);
}

TEST_F(ExtendedLogTest, ExtendedMsgConvertsToBase) {
  log_meta meta;
  meta.pid = 1;
  log_msg base_msg;
  base_msg.payload = "x";
  extended_log_msg ext(base_msg, meta);
  const log_msg &base = ext;
  EXPECT_EQ(base.payload, "x");
  EXPECT_EQ(ext.meta.pid, 1U);
}

TEST_F(ExtendedLogTest, SinkReportsFullness) {
  extended_log_sink s(bounded_store_tag{}, store);
  extended_log_sink plain(no_full_tag{}, cnt);
  EXPECT_TRUE(s.can_report_full());
  EXPECT_FALSE(plain.can_report_full());

  extended_log_msg msg;
  msg.meta.set(log_flag::can_wait);
  EXPECT_FALSE(s.full(msg));
  s.log(msg);
  s.log(msg);
  EXPECT_TRUE(s.full(msg));
  EXPECT_FALSE(plain.full(msg));
}

TEST_F(ExtendedLogTest, LoggerStampsMetadataFromCallback) {
  bool can_wait = false;
  extended_logger<> logger("t", extended_log_sink(bounded_store_tag{}, store));
  logger.set_level(level::trace);
  logger.set_meta_callback(&fake_context, &can_wait);
  logger.info("hello {}", 1);

  std::uint32_t tid = 0;
  ASSERT_TRUE(store.pop(tid));
  EXPECT_EQ(tid, 42U);
}

TEST_F(ExtendedLogTest, FanoutDiscardsWhenWaitTimesOut) {
  fanout_log_sink<2> fan;
  ASSERT_TRUE(fan.add_sink(extended_log_sink(bounded_store_tag{}, store)));
  ASSERT_TRUE(fan.add_sink(extended_log_sink(no_full_tag{}, cnt)));

  extended_log_msg msg;
  msg.meta.set(log_flag::can_wait);
  auto sink = fan.as_sink();
  sink.log(msg);
  sink.log(msg);
  EXPECT_EQ(cnt.n, 2);

  sink.log(msg); // store is full and the null channel cannot wait
  EXPECT_EQ(fan.dropped(), 1U);
  EXPECT_EQ(cnt.n, 2);
  EXPECT_TRUE(sink.full(msg));
}

TEST_F(ExtendedLogTest, FanoutWritesThroughWhenContextCannotWait) {
  fanout_log_sink<2> fan;
  ASSERT_TRUE(fan.add_sink(extended_log_sink(bounded_store_tag{}, store)));
  auto sink = fan.as_sink();

  extended_log_msg msg; // can_wait clear: IRQ-like
  sink.log(msg);
  sink.log(msg);
  sink.log(msg);
  EXPECT_EQ(fan.dropped(), 0U);
  EXPECT_EQ(store.overwritten_, 1U);
}

TEST_F(ExtendedLogTest, FanoutRefusesSinksBeyondCapacity) {
  fanout_log_sink<1> fan;
  EXPECT_TRUE(fan.add_sink(extended_log_sink(no_full_tag{}, cnt)));
  EXPECT_FALSE(fan.add_sink(extended_log_sink(no_full_tag{}, cnt)));
}

TEST_F(ExtendedLogTest, WaiterIsWokenByReader) {
  fanout_log_sink<1, condition_variable_wait_channel> fan;
  ASSERT_TRUE(fan.add_sink(extended_log_sink(bounded_store_tag{}, store)));
  fan.set_wait_timeout_us(5'000'000);
  auto sink = fan.as_sink();

  extended_log_msg msg;
  msg.meta.set(log_flag::can_wait);
  sink.log(msg);
  sink.log(msg); // now full

  std::thread reader([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::uint32_t tid = 0;
    EXPECT_TRUE(store.pop(tid));
    fan.notifier().notify();
  });

  sink.log(msg); // blocks until the reader frees a slot
  reader.join();
  EXPECT_EQ(fan.dropped(), 0U);
  EXPECT_TRUE(store.is_full());
}

TEST_F(ExtendedLogTest, ThreadWritersAreOrderedFifo) {
  fanout_log_sink<1, condition_variable_wait_channel> fan;
  ASSERT_TRUE(fan.add_sink(extended_log_sink(bounded_store_tag{}, store)));
  fan.set_wait_timeout_us(5'000'000);
  auto sink = fan.as_sink();

  auto make = [](std::uint32_t tid) {
    extended_log_msg m;
    m.meta.tid = tid;
    m.meta.set(log_flag::can_wait);
    return m;
  };
  sink.log(make(1));
  sink.log(make(2)); // full

  // A blocks on space; B (arriving later) blocks on its turn.
  std::thread a([&] { sink.log(make(10)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  std::thread b([&] { sink.log(make(11)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::uint32_t tid = 0;
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(store.pop(tid));
    fan.notify_space();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  a.join();
  b.join();

  const std::uint32_t expected[] = {10, 11};
  for (std::uint32_t want : expected) {
    ASSERT_TRUE(store.pop(tid));
    EXPECT_EQ(tid, want);
  }
  EXPECT_EQ(fan.dropped(), 0U);
}

TEST_F(ExtendedLogTest, PriorityWriterNeverWaitsBehindBlockedThreadWriter) {
  fanout_log_sink<1, condition_variable_wait_channel> fan;
  ASSERT_TRUE(fan.add_sink(extended_log_sink(bounded_store_tag{}, store)));
  fan.set_wait_timeout_us(5'000'000);
  auto sink = fan.as_sink();

  extended_log_msg thread_msg;
  thread_msg.meta.set(log_flag::can_wait);
  sink.log(thread_msg);
  sink.log(thread_msg); // full

  std::thread blocked([&] { sink.log(thread_msg); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  extended_log_msg irq; // can_wait clear
  sink.log(irq);        // returns immediately and overwrites
  EXPECT_EQ(store.overwritten_, 1U);

  std::uint32_t tid = 0;
  ASSERT_TRUE(store.pop(tid));
  fan.notify_space();
  blocked.join();
  EXPECT_EQ(fan.dropped(), 0U);
}
