// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <microfmt/log/log_msg.hpp>
#include <microfmt/sinks/async_queue_sink.hpp>
#include <microfmt/sinks/journald_native_sink.hpp>
#include <microfmt/sinks/rotating_file_sink.hpp>

namespace {

// In-memory file system standing in for the real one.
struct fake_fs {
  std::map<std::string, std::string> files;
  std::map<int, std::string> open_fds;
  int next_fd{3};
};

fake_fs fs;

int fs_open(const char *path) noexcept {
  fs.files[path];
  const int fd = fs.next_fd++;
  fs.open_fds[fd] = path;
  return fd;
}

void fs_close(int fd) noexcept { fs.open_fds.erase(fd); }

bool fs_rename(const char *from, const char *to) noexcept {
  const auto it = fs.files.find(from);
  if (it == fs.files.end()) {
    return false;
  }
  const std::string content = it->second;
  fs.files.erase(it);
  fs.files[to] = content;
  return true;
}

std::size_t fs_size(int fd) noexcept { return fs.files[fs.open_fds[fd]].size(); }

std::ptrdiff_t fs_write(int fd, microfmt::string_view data) noexcept {
  fs.files[fs.open_fds[fd]].append(data.data(), data.size());
  return static_cast<std::ptrdiff_t>(data.size());
}

microfmt::rotating_file_ops fake_ops() { return {fs_open, fs_close, fs_rename, fs_size, fs_write}; }

std::string datagram;
bool send_ok = true;

bool capture_datagram(int, microfmt::string_view d) noexcept {
  datagram.assign(d.data(), d.size());
  return send_ok;
}

} // namespace

TEST(AsyncQueueSinkTest, DrainsInOrderAcrossWraparound) {
  microfmt::async_queue_sink<8> queue;
  std::string out;
  auto append = [&out](microfmt::string_view sv) noexcept { out.append(sv.data(), sv.size()); };
  microfmt::callback_sink drain_to(append);

  queue.write("abcdef");
  EXPECT_EQ(queue.drain(drain_to.as_sink(), 4), 4U);
  queue.write("ghij"); // wraps around the end of the storage
  EXPECT_EQ(queue.drain(drain_to.as_sink()), 6U);

  EXPECT_EQ(out, "abcdefghij");
  EXPECT_EQ(queue.size(), 0U);
}

TEST(AsyncQueueSinkTest, KeepsOldestAndCountsDroppedWhenFull) {
  microfmt::async_queue_sink<4> queue;
  queue.write("abcdef");
  EXPECT_EQ(queue.dropped_bytes(), 2U);

  std::string out;
  auto append = [&out](microfmt::string_view sv) noexcept { out.append(sv.data(), sv.size()); };
  microfmt::callback_sink drain_to(append);
  queue.drain(drain_to.as_sink());
  EXPECT_EQ(out, "abcd");
}

TEST(JournaldNativeSinkTest, BuildsTextFormatDatagram) {
  datagram.clear();
  send_ok = true;
  microfmt::log::journald_native_sink<256> journal(5, capture_datagram);
  auto output = journal.as_sink();
  output.log(microfmt::log::log_msg{.logger_name = "net", .lvl = microfmt::log::level::err, .payload = "link down"});

  EXPECT_EQ(datagram, "PRIORITY=3\nSYSLOG_IDENTIFIER=net\nMESSAGE=link down\n");
}

TEST(JournaldNativeSinkTest, UsesBinaryFormForMultilineMessages) {
  datagram.clear();
  send_ok = true;
  microfmt::log::journald_native_sink<256> journal(5, capture_datagram);
  auto output = journal.as_sink();
  output.log(microfmt::log::log_msg{.lvl = microfmt::log::level::info, .payload = "a\nb"});

  const std::string expected = std::string("PRIORITY=6\nMESSAGE\n") + std::string("\x03\0\0\0\0\0\0\0", 8) + "a\nb\n";
  EXPECT_EQ(datagram, expected);
}

TEST(JournaldNativeSinkTest, TruncatesLongMessageAndCountsSendFailure) {
  datagram.clear();
  send_ok = false;
  microfmt::log::journald_native_sink<64> journal(5, capture_datagram);
  auto output = journal.as_sink();
  const std::string payload(200, 'x');
  output.log(microfmt::log::log_msg{.lvl = microfmt::log::level::warn, .payload = payload});

  EXPECT_EQ(datagram.size(), 64U);
  EXPECT_EQ(datagram.back(), '\n');
  EXPECT_EQ(journal.dropped_records(), 1U);
  send_ok = true;
}

TEST(RotatingFileSinkTest, RotatesBeforeExceedingLimitAndShiftsBackups) {
  fs = {};
  microfmt::rotating_file_sink<64> sink("app.log", 10, 2, fake_ops());
  sink.write("aaaaaa");
  sink.write("bbbbbb"); // 6 + 6 > 10: rotate first
  sink.write("cccccc"); // rotate again, backups shift
  sink.write("dddd");   // fits (6 + 4)

  EXPECT_EQ(fs.files["app.log"], "ccccccdddd");
  EXPECT_EQ(fs.files["app.log.1"], "bbbbbb");
  EXPECT_EQ(fs.files["app.log.2"], "aaaaaa");
  EXPECT_EQ(sink.dropped_bytes(), 0U);
}

TEST(RotatingFileSinkTest, OldestBackupIsOverwritten) {
  fs = {};
  microfmt::rotating_file_sink<64> sink("a.log", 4, 1, fake_ops());
  sink.write("1111");
  sink.write("2222");
  sink.write("3333");

  EXPECT_EQ(fs.files["a.log"], "3333");
  EXPECT_EQ(fs.files["a.log.1"], "2222");
  EXPECT_EQ(fs.files.count("a.log.2"), 0U);
}

TEST(RotatingFileSinkTest, OversizedWriteGoesWholeIntoFreshFile) {
  fs = {};
  microfmt::rotating_file_sink<64> sink("a.log", 4, 1, fake_ops());
  sink.write("12");
  sink.write("0123456789");

  EXPECT_EQ(fs.files["a.log"], "0123456789");
  EXPECT_EQ(fs.files["a.log.1"], "12");
}

TEST(RotatingFileSinkTest, ResumesSizeOfExistingFile) {
  fs = {};
  fs.files["a.log"] = "xxxxxxxx";
  microfmt::rotating_file_sink<64> sink("a.log", 10, 1, fake_ops());
  EXPECT_EQ(sink.current_size(), 8U);
  sink.write("yyy");

  EXPECT_EQ(fs.files["a.log.1"], "xxxxxxxx");
  EXPECT_EQ(fs.files["a.log"], "yyy");
}
