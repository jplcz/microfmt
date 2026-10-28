// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Parses a "name = value;" file much larger than the memory it is given: a 512-byte window is refilled from a
// reader that hands out 5 bytes per call, consumed input is dropped at every statement end, and names and string
// values live in a string pool.

#include <cstdint>
#include <cstdio>
#include <string>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/string_pool.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

// Stands in for a file, a socket or a UART: fills the span it is given and returns how many bytes it wrote.
// Returning fewer bytes than asked is normal; the stream keeps asking until it has what it needs. 0 means the end.
struct slow_reader {
  const std::string &data;
  size_t pos{0};

  size_t operator()(reloco::span<char> out) noexcept {
    size_t n = data.size() - pos;
    n = n < 5 ? n : 5;
    n = n < out.size() ? n : out.size();
    for (size_t i = 0; i < n; ++i) {
      out[i] = data[pos + i];
    }
    pos += n;
    return n;
  }
};

// `name = "text";` or `name = 42;` -- both leave plain data (ids), nothing that points into the window.
struct setting {
  microfmt::string_id name;
  microfmt::string_id text; // invalid() for a number
  int64_t number{0};
};

reloco::expected<setting, microfmt::parse_error> parse_setting(microfmt::token_stream &ts) {
  setting result;

  // The name is interned: equal spellings share one id, so 3000 settings named "k0".."k9" cost ten strings.
  auto name = microfmt::expect_name(ts, "expected a setting name");
  if (!name) {
    return reloco::unexpected<microfmt::parse_error>(name.error());
  }
  result.name = name.value();

  if (auto eq = ts.expect(microfmt::token_kind::op, "=", "expected '='"); !eq) {
    return reloco::unexpected<microfmt::parse_error>(eq.error());
  }

  if (ts.check(microfmt::token_kind::string)) {
    // The literal's escapes are decoded into the pool; `\n` in the file becomes a real newline in the value.
    auto text = microfmt::expect_string(ts, "expected a string", microfmt::decode_c_escapes);
    if (!text) {
      return reloco::unexpected<microfmt::parse_error>(text.error());
    }
    result.text = text.value();
  } else {
    auto number = ts.expect(microfmt::token_kind::integer, "expected a string or a number");
    if (!number) {
      return reloco::unexpected<microfmt::parse_error>(number.error());
    }
    result.number = static_cast<int64_t>(number.value().text.size()); // the demo only needs *a* value
  }

  if (auto semi = ts.expect(microfmt::token_kind::punctuation, ";", "expected ';'"); !semi) {
    return reloco::unexpected<microfmt::parse_error>(semi.error());
  }
  return result;
}

} // namespace

int main() {
  // Build roughly 60 KB of input with a few mistakes in it.
  std::string text;
  for (int i = 0; i < 3000; ++i) {
    text += "k" + std::to_string(i % 10) + " = ";
    text += (i % 3 == 0) ? "\"line\\none\"" : std::to_string(i);
    text += (i == 1000 || i == 2000) ? " oops\n" : ";\n"; // two missing semicolons
  }

  slow_reader reader{text};
  microfmt::stream_window<512> window{reader}; // 512 bytes is all the memory the input ever gets

  microfmt::string_pool<16> pool;
  const microfmt::string_pool_ref strings = pool.ref(); // what the stream interns into

  microfmt::lexer_rules rules;
  static constexpr microfmt::string_view kOps[] = {"="};
  rules.operators = kOps;
  microfmt::token_stream ts{window.ref(), rules, strings};

  microfmt::error_list<8> errors;
  static constexpr microfmt::token_sync kSync[] = {{microfmt::token_kind::punctuation, ";"}};

  size_t parsed = 0;
  size_t peak = 0;
  while (!ts.at_end()) {
    const auto one = parse_setting(ts);
    if (one) {
      ++parsed;
    } else {
      // Record the error (copied out of the window), skip to the next ';' and consume it.
      errors.add(one.error());
      (void)microfmt::recover_to(ts, kSync);
      if (ts.check(microfmt::token_kind::punctuation, ";")) {
        ts.next();
      }
    }
    peak = window.ref().used() > peak ? window.ref().used() : peak;
    // Statement boundary: drop everything consumed so far, so the window never fills up.
    ts.discard_consumed();
  }

  std::printf("input:        %zu bytes\n", text.size());
  std::printf("parsed:       %zu statements, %zu errors\n", parsed, errors.total());
  std::printf("window peak:  %zu bytes of 512\n", peak);
  std::printf("pool:         %zu strings, %zu bytes\n", pool.size(), pool.bytes());
  std::printf("window moved: %zu bytes discarded\n", window.ref().base());

  // The errors were stored as plain data, so they can still be printed long after their text left the window.
  // `report` notices the text is gone and prints just "message (line:column)".
  const auto kept = errors.errors();
  microfmt::buffer_sink<512> out;
  for (size_t i = 0; i < kept.size(); ++i) {
    microfmt::format_to(out.as_sink(), "error {}: {}\n", i + 1,
                        microfmt::report(kept[i], ts.source(), ts.source_base()));
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;
  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());
  RELOCO_END_UNSAFE_BUFFER_USAGE;

  return 0;
}
