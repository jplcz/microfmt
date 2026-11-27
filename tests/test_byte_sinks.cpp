// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <string>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/posix_fd_sink.hpp>
#include <microfmt/sinks/socket_sink.hpp>
#include <microfmt/sinks/win_debug_sink.hpp>

namespace {

// Scripted fake transport: each call consumes the next step.
struct step {
  std::ptrdiff_t result;
  int err;
};

struct fake_transport {
  std::array<step, 8> steps{};
  std::size_t step_count{0};
  std::size_t calls{0};
  std::string received;
  int last_fd{-1};

  std::ptrdiff_t run(int fd, microfmt::string_view data) noexcept {
    last_fd = fd;
    const step s = calls < step_count ? steps[calls] : step{-1, EIO};
    ++calls;
    if (s.result < 0) {
      errno = s.err;
      return -1;
    }
    const auto n = static_cast<std::size_t>(s.result) < data.size() ? static_cast<std::size_t>(s.result) : data.size();
    received.append(data.data(), n);
    return static_cast<std::ptrdiff_t>(n);
  }
};

fake_transport transport;

std::ptrdiff_t fake_io(int fd, microfmt::string_view data) noexcept { return transport.run(fd, data); }

void script(std::initializer_list<step> list) {
  transport = {};
  for (const step s : list) {
    transport.steps[transport.step_count++] = s;
  }
}

std::string debug_chunks;
std::size_t debug_chunk_count = 0;

void capture_debug(const char *text) noexcept {
  debug_chunks += text;
  debug_chunks += '|';
  ++debug_chunk_count;
}

} // namespace

TEST(PosixFdSinkTest, ContinuesShortWritesAndEintr) {
  script({{3, 0}, {-1, EINTR}, {100, 0}});
  microfmt::posix_fd_sink sink(7, fake_io);
  sink.write("hello world");

  EXPECT_EQ(transport.received, "hello world");
  EXPECT_EQ(transport.last_fd, 7);
  EXPECT_EQ(sink.dropped_bytes(), 0U);
}

TEST(PosixFdSinkTest, DropsAndCountsBytesOnHardError) {
  script({{2, 0}, {-1, EIO}});
  microfmt::posix_fd_sink sink(1, fake_io);
  sink.write("abcdef");

  EXPECT_EQ(transport.received, "ab");
  EXPECT_EQ(sink.dropped_bytes(), 4U);
}

TEST(PosixFdSinkTest, EagainDropsByDefaultAndRetriesOnRequest) {
  script({{-1, EAGAIN}});
  microfmt::posix_fd_sink dropping(1, fake_io);
  dropping.write("xyz");
  EXPECT_EQ(dropping.dropped_bytes(), 3U);

  script({{-1, EAGAIN}, {-1, EAGAIN}, {10, 0}});
  microfmt::posix_fd_sink retrying(1, fake_io, microfmt::posix_fd_policy::retry);
  retrying.write("xyz");
  EXPECT_EQ(transport.received, "xyz");
  EXPECT_EQ(retrying.dropped_bytes(), 0U);
}

TEST(PosixFdSinkTest, ZeroLengthWriteIsDroppedInsteadOfSpinning) {
  script({{0, 0}});
  microfmt::posix_fd_sink sink(1, fake_io);
  sink.write("abc");

  EXPECT_EQ(transport.calls, 1U);
  EXPECT_EQ(sink.dropped_bytes(), 3U);
}

TEST(PosixFdSinkTest, FormatsThroughTypeErasedSink) {
  script({{100, 0}, {100, 0}, {100, 0}}); // one write per formatted piece
  microfmt::posix_fd_sink sink(1, fake_io);
  microfmt::format_to(sink.as_sink(), "{}-{}", 1, "two");

  EXPECT_EQ(transport.received, "1-two");
}

TEST(PosixFdSinkTest, LogAdapterWritesOneLinePerRecord) {
  script({{100, 0}});
  microfmt::posix_fd_sink target(1, fake_io);
  microfmt::log::posix_fd_log_sink<64> adapter(target);
  auto output = adapter.as_sink();
  output.log(microfmt::log::log_msg{.logger_name = "net", .lvl = microfmt::log::level::info, .payload = "up"});

  EXPECT_EQ(transport.received, "[net] up\n");
}

TEST(SocketSinkTest, DatagramModeSendsOnePacketPerWrite) {
  script({{5, 0}});
  microfmt::socket_sink sink(4, fake_io);
  sink.write("hello");

  EXPECT_EQ(transport.calls, 1U);
  EXPECT_EQ(transport.received, "hello");
  EXPECT_EQ(sink.dropped_bytes(), 0U);
}

TEST(SocketSinkTest, DatagramModeDoesNotResendAfterFailure) {
  script({{-1, ENOBUFS}});
  microfmt::socket_sink sink(4, fake_io);
  sink.write("hello");

  EXPECT_EQ(transport.calls, 1U);
  EXPECT_EQ(sink.dropped_bytes(), 5U);
}

TEST(SocketSinkTest, StreamModeContinuesShortSends) {
  script({{2, 0}, {-1, EINTR}, {10, 0}});
  microfmt::socket_sink sink(4, fake_io, microfmt::socket_sink_mode::stream);
  sink.write("hello");

  EXPECT_EQ(transport.received, "hello");
  EXPECT_EQ(sink.dropped_bytes(), 0U);
}

TEST(SocketSinkTest, LogAdapterAddsNewlineOnlyWhenRequested) {
  script({{100, 0}});
  microfmt::socket_sink target(4, fake_io);
  microfmt::log::socket_log_sink<64> packet(target, false);
  auto output = packet.as_sink();
  output.log(microfmt::log::log_msg{.lvl = microfmt::log::level::warn, .payload = "low"});
  EXPECT_EQ(transport.received, "low");

  script({{100, 0}});
  microfmt::log::socket_log_sink<64> stream(target);
  auto stream_output = stream.as_sink();
  stream_output.log(microfmt::log::log_msg{.lvl = microfmt::log::level::warn, .payload = "low"});
  EXPECT_EQ(transport.received, "low\n");
}

TEST(WinDebugSinkTest, SplitsLongOutputIntoNulTerminatedChunks) {
  debug_chunks.clear();
  debug_chunk_count = 0;
  microfmt::win_debug_sink<5> sink(capture_debug);
  sink.write("abcdefghij");

  EXPECT_EQ(debug_chunks, "abcd|efgh|ij|");
  EXPECT_EQ(debug_chunk_count, 3U);
}

TEST(WinDebugSinkTest, LogAdapterSendsRecordLine) {
  debug_chunks.clear();
  microfmt::win_debug_sink<64> target(capture_debug);
  microfmt::log::win_debug_log_sink<64> adapter(target);
  auto output = adapter.as_sink();
  output.log(microfmt::log::log_msg{.logger_name = "ui", .lvl = microfmt::log::level::err, .payload = "boom"});

  EXPECT_EQ(debug_chunks, "[ui] boom\n|");
}
