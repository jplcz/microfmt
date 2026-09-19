// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <array>
#include <cstdio>
#include <gtest/gtest.h>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

#if MICROFMT_HAS_POSIX_FD
#include <unistd.h>
#endif

TEST(StdioTest, FileSinkMemstream) {
  char mem_buf[64] = {};

#if defined(__GLIBC__) || defined(__APPLE__)
  // Use POSIX fmemopen to test without touching the filesystem
  std::FILE *fp = fmemopen(mem_buf, sizeof(mem_buf), "w");
  ASSERT_NE(fp, nullptr);

  microfmt::print(fp, "Status: {}, Code: 0x{:02x}", "OK", 0x1F);
  std::fflush(fp);
  std::fclose(fp);

  EXPECT_STREQ(mem_buf, "Status: OK, Code: 0x1f");
#endif
}

TEST(StdioTest, FilePrintlnAppendsNewline) {
#if defined(__GLIBC__) || defined(__APPLE__)
  char mem_buf[64] = {};

  std::FILE *fp = fmemopen(mem_buf, sizeof(mem_buf), "w");
  ASSERT_NE(fp, nullptr);

  microfmt::println(fp, "Line {:d}", 1);
  std::fflush(fp);
  std::fclose(fp);

  EXPECT_STREQ(mem_buf, "Line 1\n");
#endif
}

#if MICROFMT_HAS_POSIX_FD
TEST(StdioTest, PosixPipeSink) {
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  int pipefd[2];
  ASSERT_EQ(::pipe(pipefd), 0);

  // Write through microfmt fd_sink
  microfmt::println(pipefd[1], "FD Test: val={}", 42);
  ::close(pipefd[1]);

  // Read back and verify
  char read_buf[32] = {};
  const ssize_t bytes_read = ::read(pipefd[0], read_buf, sizeof(read_buf) - 1);
  ::close(pipefd[0]);

  ASSERT_GT(bytes_read, 0);
  read_buf[bytes_read] = '\0';
  EXPECT_STREQ(read_buf, "FD Test: val=42\n");

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
#endif
