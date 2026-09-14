// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <microfmt/formatters/cbor.hpp>

namespace {

nlohmann::json parse_cbor(std::string_view encoded) {
  std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
  return nlohmann::json::from_cbor(bytes);
}

} // namespace

TEST(CborWriterTest, EncodesNestedMapAndArrayRoundTrip) {
  const uint8_t identifier[] = {0xDE, 0xAD, 0xBE, 0xEF};
  microfmt::buffer_sink<512> output;
  {
    microfmt::cbor::map_writer document(output.as_sink());
    document.kv("device", "sensor-7")
        .kv("enabled", true)
        .kv("identifier", microfmt::span(identifier));

    {
      auto readings = document.nested_array("readings");
      readings.val(-12).val(uint8_t{42}).val(nullptr);
    }

    {
      auto metadata = document.nested_map("metadata");
      metadata.kv("firmware", 7).kv("version", 3);
    }
  }

  const auto document = parse_cbor(output.view());
  EXPECT_EQ(document["device"], "sensor-7");
  EXPECT_EQ(document["enabled"], true);
  EXPECT_EQ(document["identifier"].get_binary(),
            nlohmann::json::binary_t({0xDE, 0xAD, 0xBE, 0xEF}));
  EXPECT_EQ(document["readings"], nlohmann::json::array({-12, 42, nullptr}));
  EXPECT_EQ(document["metadata"]["firmware"], 7);
  EXPECT_EQ(document["metadata"]["version"], 3);
}

TEST(CborWriterTest, EncodesCompactIntegerKey) {
  microfmt::buffer_sink<16> output;
  {
    microfmt::cbor::map_writer document(output.as_sink());
    document.kv(uint32_t{1}, uint8_t{7});
  }

  const std::string_view encoded = output.view();
  ASSERT_EQ(encoded.size(), 4U);
  EXPECT_EQ(static_cast<uint8_t>(encoded[0]), 0xBF);
  EXPECT_EQ(static_cast<uint8_t>(encoded[1]), 0x01);
  EXPECT_EQ(static_cast<uint8_t>(encoded[2]), 0x07);
  EXPECT_EQ(static_cast<uint8_t>(encoded[3]), 0xFF);
}

TEST(CborWriterTest, EncodesIntegerBoundariesAndNullCString) {
  const char *null_text = nullptr;
  microfmt::buffer_sink<256> output;
  {
    microfmt::cbor::array_writer values(output.as_sink());
    values.val(std::numeric_limits<int64_t>::min())
        .val(std::numeric_limits<uint64_t>::max())
        .val(null_text)
        .val("");
  }

  const auto values = parse_cbor(output.view());
  EXPECT_EQ(values.at(0), std::numeric_limits<int64_t>::min());
  EXPECT_EQ(values.at(1), std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(values.at(2).is_null());
  EXPECT_EQ(values.at(3), "");
}

TEST(CborWriterTest, EmbedsLambdaGeneratedMapInFormatString) {
  microfmt::buffer_sink<128> output;
  microfmt::format_to(
      output.as_sink(), "{}",
      microfmt::cbor::cbor_map([](microfmt::cbor::map_writer &event) {
        event.kv("kind", "boot").kv("sequence", 7);
      }));

  const auto event = parse_cbor(output.view());
  EXPECT_EQ(event["kind"], "boot");
  EXPECT_EQ(event["sequence"], 7);
}
