// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include <microfmt/formatters/json.hpp>

RELOCO_BEGIN_UNSAFE_BUFFER_USAGE

#include <nlohmann/json.hpp>

RELOCO_END_UNSAFE_BUFFER_USAGE

TEST(JsonFormatterTest, ObjectAndArrayRoundTrip) {
  microfmt::buffer_sink<512> output;
  {
    microfmt::json::object_writer document(output.as_sink());
    document.kv("device", "sensor\nnode").kv("enabled", true).kv("retries", uint8_t{3});

    {
      auto measurements = document.nested_array("measurements");
      measurements.val(-12).val(42).val(nullptr);
    }

    {
      auto status = document.nested_object("status");
      status.kv("code", 200).kv("message", "ok");
    }
  }

  const auto document = nlohmann::json::parse(std::string(output.view().data(), output.view().size()));

  EXPECT_EQ(document["device"], "sensor\nnode");
  EXPECT_EQ(document["enabled"], true);
  EXPECT_EQ(document["retries"], 3);
  EXPECT_EQ(document["measurements"], nlohmann::json::array({-12, 42, nullptr}));
  EXPECT_EQ(document["status"]["code"], 200);
  EXPECT_EQ(document["status"]["message"], "ok");
}

TEST(JsonFormatterTest, HandlesMinimumSignedIntegerWithoutOverflow) {
  microfmt::buffer_sink<128> output;
  {
    microfmt::json::array_writer values(output.as_sink());
    values.val(std::numeric_limits<int64_t>::min());
  }

  const auto document = nlohmann::json::parse(std::string(output.view().data(), output.view().size()));
  EXPECT_EQ(document.at(0), std::numeric_limits<int64_t>::min());
}

TEST(JsonFormatterTest, ComplexEscapedPayloadRoundTrips) {
  const char *null_text = nullptr;
  const microfmt::string_view escaped = "quote=\" slash=\\ newline=\n tab=\t "
                                        "backspace=\b formfeed=\f control=\x01";
  const microfmt::string_view unicode = "temperature: 23 \xC2\xB0"
                                        "C";
  microfmt::buffer_sink<2048> output;
  {
    microfmt::json::object_writer document(output.as_sink());
    document.kv("empty", "")
        .kv("null_text", null_text)
        .kv("escaped", escaped)
        .kv("minimum", std::numeric_limits<int64_t>::min())
        .kv("maximum", std::numeric_limits<uint64_t>::max());

    {
      auto metadata = document.nested_object("metadata");
      metadata.kv("path", "/dev/sensor\\primary").kv("enabled", false).kv("unicode", unicode);
    }

    {
      auto readings = document.nested_array("readings");
      readings.val(nullptr).val(null_text).val("").val(-1).val(uint64_t{18446744073709551615ULL});
    }
  }

  const auto document = nlohmann::json::parse(std::string(output.view().data(), output.view().size()));

  EXPECT_EQ(document["empty"], "");
  EXPECT_TRUE(document["null_text"].is_null());
  EXPECT_EQ(document["escaped"].get<std::string>(), std::string(escaped.data(), escaped.size()));
  EXPECT_EQ(document["minimum"], std::numeric_limits<int64_t>::min());
  EXPECT_EQ(document["maximum"], std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(document["metadata"]["path"], "/dev/sensor\\primary");
  EXPECT_EQ(document["metadata"]["enabled"], false);
  EXPECT_EQ(document["metadata"]["unicode"].get<std::string>(), std::string(unicode.data(), unicode.size()));
  EXPECT_TRUE(document["readings"][0].is_null());
  EXPECT_TRUE(document["readings"][1].is_null());
  EXPECT_EQ(document["readings"][2], "");
  EXPECT_EQ(document["readings"][3], -1);
  EXPECT_EQ(document["readings"][4], std::numeric_limits<uint64_t>::max());
}

TEST(JsonFormatterTest, EmbedsLambdaGeneratedObjectInFormatString) {
  const auto formatted =
      microfmt::format<128>("event={}", microfmt::json::json_obj([](microfmt::json::object_writer &event) {
                              event.as_known().kv("kind", "boot").kv("sequence", 7);
                            }));

  const auto document =
      nlohmann::json::parse(std::string(formatted.view().substr(microfmt::string_view("event=").size())));
  EXPECT_EQ(document["kind"], "boot");
  EXPECT_EQ(document["sequence"], 7);
}
