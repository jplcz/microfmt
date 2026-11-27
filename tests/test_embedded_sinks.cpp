// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <microfmt/hw/itm_sink.hpp>
#include <microfmt/hw/rtt_sink.hpp>
#include <microfmt/hw/uart_sink.hpp>
#include <microfmt/log/log_msg.hpp>
#include <microfmt/log/sink_adapter.hpp>
#include <microfmt/sinks/chunked_sink.hpp>

namespace {

struct itm_store {
  std::uint32_t value;
  std::uint8_t width;
};

std::vector<itm_store> itm_stores;
std::uint32_t itm_busy_polls = 0;

bool itm_ready(std::uintptr_t) noexcept {
  if (itm_busy_polls > 0) {
    --itm_busy_polls;
    return false;
  }
  return true;
}

void itm_write(std::uintptr_t, std::uint32_t value, std::uint8_t width) noexcept {
  itm_stores.push_back({value, width});
}

struct fake_uart {
  std::string sent;
  std::uint32_t busy_polls{0};
};

bool uart_ready(void *ctx) noexcept {
  auto *u = static_cast<fake_uart *>(ctx);
  if (u->busy_polls > 0) {
    --u->busy_polls;
    return false;
  }
  return true;
}

void uart_put(void *ctx, std::uint8_t byte) noexcept {
  static_cast<fake_uart *>(ctx)->sent.push_back(static_cast<char>(byte));
}

void collect_chunk(void *ctx, microfmt::string_view chunk) noexcept {
  static_cast<std::vector<std::string> *>(ctx)->emplace_back(chunk.data(), chunk.size());
}

} // namespace

TEST(RttSinkTest, ControlBlockHasSeggerIdAndLayout) {
  microfmt::rtt_sink<16> rtt;
  const auto &cb = rtt.control_block();

  EXPECT_EQ(std::string(cb.id, 10), "SEGGER RTT");
  EXPECT_EQ(cb.max_up_buffers, 1);
  EXPECT_EQ(cb.max_down_buffers, 0);
  EXPECT_EQ(cb.up.size, 16U);
  EXPECT_EQ(cb.up.buffer, rtt.storage().data());
}

TEST(RttSinkTest, TrimsWhenFullThenWrapsAfterProbeDrains) {
  microfmt::rtt_sink<8> rtt;
  rtt.write("abc");
  EXPECT_EQ(rtt.control_block().up.wr_off, 3U);

  rtt.write("defghij"); // only 4 bytes are free (one slot stays empty)
  EXPECT_EQ(rtt.control_block().up.wr_off, 7U);
  EXPECT_EQ(rtt.dropped_bytes(), 3U);
  EXPECT_EQ(rtt.storage().substr(0, 7), "abcdefg");

  rtt.control_block().up.rd_off = 7; // the probe read everything
  rtt.write("xy");
  EXPECT_EQ(rtt.control_block().up.wr_off, 1U);
  EXPECT_EQ(rtt.storage()[7], 'x');
  EXPECT_EQ(rtt.storage()[0], 'y');
}

TEST(RttSinkTest, FormatsThroughTypeErasedSink) {
  microfmt::rtt_sink<32> rtt;
  microfmt::format_to(rtt.as_sink(), "v={}", 42);

  EXPECT_EQ(rtt.storage().substr(0, 4), "v=42");
}

TEST(ItmSinkTest, UsesWidestAlignedStoresLittleEndian) {
  itm_stores.clear();
  itm_busy_polls = 0;
  microfmt::itm_sink itm(microfmt::itm_sink::port_address(0), itm_ready, itm_write);
  itm.write("ABCDEFG");

  ASSERT_EQ(itm_stores.size(), 3U);
  EXPECT_EQ(itm_stores[0].width, 4);
  EXPECT_EQ(itm_stores[0].value, 0x44434241U);
  EXPECT_EQ(itm_stores[1].width, 2);
  EXPECT_EQ(itm_stores[1].value, 0x4645U);
  EXPECT_EQ(itm_stores[2].width, 1);
  EXPECT_EQ(itm_stores[2].value, 0x47U);
}

TEST(ItmSinkTest, PortAddressIsBasePlusFourPerPort) {
  EXPECT_EQ(microfmt::itm_sink::port_address(0), 0xE0000000U);
  EXPECT_EQ(microfmt::itm_sink::port_address(3), 0xE000000CU);
}

TEST(ItmSinkTest, WaitsForFifoThenDropsAfterSpinLimit) {
  itm_stores.clear();
  itm_busy_polls = 3;
  microfmt::itm_sink waiting(0, itm_ready, itm_write, 10);
  waiting.write("AB");
  EXPECT_EQ(itm_stores.size(), 1U);
  EXPECT_EQ(waiting.dropped_bytes(), 0U);

  itm_stores.clear();
  itm_busy_polls = 1000;
  microfmt::itm_sink stuck(0, itm_ready, itm_write, 2);
  stuck.write("ABCD");
  EXPECT_TRUE(itm_stores.empty());
  EXPECT_EQ(stuck.dropped_bytes(), 4U);
}

TEST(UartSinkTest, ExpandsNewlineWhenRequested) {
  fake_uart uart;
  microfmt::uart_sink sink(&uart, uart_ready, uart_put);
  sink.write("a\nb");
  EXPECT_EQ(uart.sent, "a\r\nb");

  fake_uart raw;
  microfmt::uart_sink plain(&raw, uart_ready, uart_put, false);
  plain.write("a\nb");
  EXPECT_EQ(raw.sent, "a\nb");
}

TEST(UartSinkTest, DropsRemainderWhenTransmitterStaysBusy) {
  fake_uart uart;
  uart.busy_polls = 1000;
  microfmt::uart_sink sink(&uart, uart_ready, uart_put, true, 5);
  sink.write("hello");

  EXPECT_TRUE(uart.sent.empty());
  EXPECT_EQ(sink.dropped_bytes(), 5U);
}

TEST(ChunkedSinkTest, SendsFullChunksAndKeepsRemainderUntilFlush) {
  std::vector<std::string> chunks;
  microfmt::chunked_sink<4> sink(&chunks, collect_chunk);
  sink.write("abcdefghij");

  ASSERT_EQ(chunks.size(), 2U);
  EXPECT_EQ(chunks[0], "abcd");
  EXPECT_EQ(chunks[1], "efgh");
  EXPECT_EQ(sink.pending(), 2U);

  sink.flush();
  ASSERT_EQ(chunks.size(), 3U);
  EXPECT_EQ(chunks[2], "ij");
  sink.flush(); // nothing pending: no empty packet
  EXPECT_EQ(chunks.size(), 3U);
}

TEST(ChunkedSinkTest, FlushOnNewlineSendsShortChunk) {
  std::vector<std::string> chunks;
  microfmt::chunked_sink<8> sink(&chunks, collect_chunk, true);
  sink.write("hi\nyo");

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks[0], "hi\n");
  EXPECT_EQ(sink.pending(), 2U);
}

TEST(SinkLogAdapterTest, WritesOneLinePerRecordToBorrowedSink) {
  std::vector<std::string> chunks;
  microfmt::chunked_sink<64> target(&chunks, collect_chunk, true);
  microfmt::log::sink_log_adapter<64> adapter(target.as_sink());
  auto output = adapter.as_sink();
  output.log(microfmt::log::log_msg{.logger_name = "usb", .lvl = microfmt::log::level::info, .payload = "ready"});

  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks[0], "[usb] ready\n");
}
