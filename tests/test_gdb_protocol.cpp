// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <microfmt/inspector/gdb_decoders.hpp>
#include <microfmt/inspector/gdb_encoders.hpp>
#include <microfmt/inspector/gdb_stream.hpp>

#include <cstddef>
#include <cstdint>

namespace {

TEST(GdbClientProtocol, EncodesAndDecodesMemoryRead) {
  microfmt::gdb::client_request_view request;
  request.addr = 0x1234;
  request.length = 0x20;
  microfmt::buffer_sink<32> encoded;

  microfmt::gdb::client_request_encoder::encode(encoded.as_sink(), microfmt::gdb::packet_type::read_memory, request);
  EXPECT_EQ(encoded.view(), "m1234,20");

  std::byte storage[32]{};
  microfmt::scratch_allocator scratch(storage);
  microfmt::gdb::client_request_view decoded;
  const auto type = microfmt::gdb::client_request_decoder::decode(encoded.view(), scratch, decoded);

  EXPECT_EQ(type, microfmt::gdb::packet_type::read_memory);
  EXPECT_EQ(decoded.addr, 0x1234U);
  EXPECT_EQ(decoded.length, 0x20U);
}

TEST(GdbClientProtocol, RoundTripsHexAndBinaryWrites) {
  MICROFMT_BEGIN_UNSAFE_BUFFER_USAGE;

  const uint8_t hex_data[]{0x00, 0x23, 0x7d};
  microfmt::gdb::client_request_view hex_request;
  hex_request.addr = 0x1000;
  hex_request.length = sizeof(hex_data);
  hex_request.data = hex_data;
  microfmt::buffer_sink<64> hex_encoded;

  microfmt::gdb::client_request_encoder::encode(hex_encoded.as_sink(), microfmt::gdb::packet_type::write_memory_hex,
                                                hex_request);
  EXPECT_EQ(hex_encoded.view(), "M1000,3:00237d");

  std::byte hex_storage[16]{};
  microfmt::scratch_allocator hex_scratch(hex_storage);
  microfmt::gdb::client_request_view hex_decoded;
  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode(hex_encoded.view(), hex_scratch, hex_decoded),
            microfmt::gdb::packet_type::write_memory_hex);
  ASSERT_EQ(hex_decoded.data.size(), sizeof(hex_data));
  for (std::size_t i = 0; i < sizeof(hex_data); ++i) {
    EXPECT_EQ(hex_decoded.data[i], hex_data[i]);
  }

  const uint8_t binary_data[]{'A', '#', '$', '}', '*'};
  microfmt::gdb::client_request_view binary_request;
  binary_request.addr = 0x2000;
  binary_request.length = sizeof(binary_data);
  binary_request.data = binary_data;
  microfmt::buffer_sink<64> binary_encoded;

  microfmt::gdb::client_request_encoder::encode(binary_encoded.as_sink(),
                                                microfmt::gdb::packet_type::write_memory_binary, binary_request);

  std::byte binary_storage[16]{};
  microfmt::scratch_allocator binary_scratch(binary_storage);
  microfmt::gdb::client_request_view binary_decoded;
  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode(binary_encoded.view(), binary_scratch, binary_decoded),
            microfmt::gdb::packet_type::write_memory_binary);
  ASSERT_EQ(binary_decoded.data.size(), sizeof(binary_data));
  for (std::size_t i = 0; i < sizeof(binary_data); ++i) {
    EXPECT_EQ(binary_decoded.data[i], binary_data[i]);
  }

  MICROFMT_END_UNSAFE_BUFFER_USAGE;
}

TEST(GdbClientProtocol, DecodesSearchRegisterAndThreadRequests) {
  const uint8_t pattern[]{0xde, 0xad};
  microfmt::gdb::client_request_view search;
  search.addr = 0x4000;
  search.length = 0x40;
  search.data = pattern;
  microfmt::buffer_sink<64> encoded;
  microfmt::gdb::client_request_encoder::encode(encoded.as_sink(), microfmt::gdb::packet_type::search_memory, search);
  EXPECT_EQ(encoded.view(), "qSearch:memory:4000;40;dead");

  std::byte storage[16]{};
  microfmt::scratch_allocator scratch(storage);
  microfmt::gdb::client_request_view decoded;
  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode(encoded.view(), scratch, decoded),
            microfmt::gdb::packet_type::search_memory);
  EXPECT_EQ(decoded.addr, 0x4000U);
  EXPECT_EQ(decoded.length, 0x40U);
  ASSERT_EQ(decoded.data.size(), 2U);
  EXPECT_EQ(decoded.data[0], 0xdeU);
  EXPECT_EQ(decoded.data[1], 0xadU);

  microfmt::scratch_allocator empty_scratch(microfmt::span<std::byte>{});
  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode("p1f", empty_scratch, decoded),
            microfmt::gdb::packet_type::read_single_register);
  EXPECT_EQ(decoded.reg_index, 0x1fU);

  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode("Hg-1", empty_scratch, decoded),
            microfmt::gdb::packet_type::set_thread);
  EXPECT_EQ(decoded.thread_action, 'g');
  EXPECT_EQ(decoded.thread_id, static_cast<uint64_t>(-1));
}

TEST(GdbClientProtocol, ReportsUnknownAndScratchExhaustion) {
  std::byte storage[1]{};
  microfmt::scratch_allocator scratch(storage);
  microfmt::gdb::client_request_view decoded;

  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode("not-a-packet", scratch, decoded),
            microfmt::gdb::packet_type::unknown);
  EXPECT_EQ(microfmt::gdb::client_request_decoder::decode("M1000,2:aabb", scratch, decoded),
            microfmt::gdb::packet_type::write_memory_hex);
  EXPECT_TRUE(decoded.data.empty());
}

TEST(GdbServerProtocol, EncodesAndClassifiesResponses) {
  microfmt::gdb::server_response_view response;
  response.status_code = 5;
  response.thread_id = 0x2a;
  response.stop_reason_extra = "pc:1000;";
  microfmt::buffer_sink<64> encoded;

  microfmt::gdb::server_response_encoder::encode(encoded.as_sink(), microfmt::gdb::server_response_type::stop_signal,
                                                 response);
  EXPECT_EQ(encoded.view(), "T05thread:2a;pc:1000;");

  microfmt::gdb::server_response_view decoded;
  EXPECT_EQ(microfmt::gdb::server_response_decoder::decode(encoded.view(), decoded),
            microfmt::gdb::server_response_type::stop_signal);
  EXPECT_EQ(decoded.status_code, 5U);
  EXPECT_EQ(decoded.stop_reason_extra, "thread:2a;pc:1000;");

  EXPECT_EQ(microfmt::gdb::server_response_decoder::decode("OK", decoded), microfmt::gdb::server_response_type::ok);
  EXPECT_EQ(microfmt::gdb::server_response_decoder::decode("E16", decoded), microfmt::gdb::server_response_type::error);
  EXPECT_EQ(decoded.status_code, 0x16U);
  EXPECT_EQ(microfmt::gdb::server_response_decoder::decode("", decoded), microfmt::gdb::server_response_type::empty);
}

TEST(GdbServerProtocol, EncodesListsConsoleOutputAndHexData) {
  const uint64_t thread_ids[]{1, 0x2a, 0xff};
  microfmt::gdb::server_response_view response;
  response.thread_ids = thread_ids;
  microfmt::buffer_sink<32> encoded;
  microfmt::gdb::server_response_encoder::encode(encoded.as_sink(), microfmt::gdb::server_response_type::thread_list,
                                                 response);
  EXPECT_EQ(encoded.view(), "m1,2a,ff");

  microfmt::gdb::server_response_view decoded;
  EXPECT_EQ(microfmt::gdb::server_response_decoder::decode(encoded.view(), decoded),
            microfmt::gdb::server_response_type::thread_list);
  EXPECT_EQ(decoded.text, "1,2a,ff");

  encoded.reset();
  response.text = "Hi";
  microfmt::gdb::server_response_encoder::encode(encoded.as_sink(), microfmt::gdb::server_response_type::console_output,
                                                 response);
  EXPECT_EQ(encoded.view(), "O4869");

  std::byte storage[8]{};
  microfmt::scratch_allocator scratch(storage);
  const auto bytes = microfmt::gdb::server_response_decoder::decode_hex_data("4869", scratch);
  ASSERT_EQ(bytes.size(), 2U);
  EXPECT_EQ(bytes[0], static_cast<uint8_t>('H'));
  EXPECT_EQ(bytes[1], static_cast<uint8_t>('i'));
}

TEST(GdbStreamProtocol, FramesAndDecodesPayload) {
  char frame_storage[5]{};
  microfmt::gdb::gdb_packet_writer writer(frame_storage);
  writer.as_sink().write("g");

  const auto frame = writer.finalize();
  EXPECT_EQ(frame, "$g#67");
  EXPECT_TRUE(writer.is_finalized());
  EXPECT_EQ(writer.finalize(), frame);

  char payload_storage[8]{};
  microfmt::gdb::gdb_streaming_decoder decoder(payload_storage);
  auto status = microfmt::gdb::gdb_streaming_decoder::status::in_progress;
  for (char c : frame) {
    status = decoder.feed(c);
  }

  EXPECT_EQ(status, microfmt::gdb::gdb_streaming_decoder::status::ready);
  EXPECT_EQ(decoder.payload(), "g");
}

TEST(GdbStreamProtocol, RejectsBadChecksumsAndOverflow) {
  char payload_storage[2]{};
  microfmt::gdb::gdb_streaming_decoder decoder(payload_storage);
  auto status = microfmt::gdb::gdb_streaming_decoder::status::in_progress;
  for (char c : microfmt::string_view("$g#00")) {
    status = decoder.feed(c);
  }
  EXPECT_EQ(status, microfmt::gdb::gdb_streaming_decoder::status::error_checksum);

  decoder.reset();
  EXPECT_EQ(decoder.feed('$'), microfmt::gdb::gdb_streaming_decoder::status::in_progress);
  EXPECT_EQ(decoder.feed('a'), microfmt::gdb::gdb_streaming_decoder::status::in_progress);
  EXPECT_EQ(decoder.feed('b'), microfmt::gdb::gdb_streaming_decoder::status::in_progress);
  EXPECT_EQ(decoder.feed('c'), microfmt::gdb::gdb_streaming_decoder::status::error_overflow);
}

} // namespace
