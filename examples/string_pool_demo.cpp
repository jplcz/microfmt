// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Names and literals in a tree: a string_pool stores each distinct string once and nodes keep a 4-byte id.
// The demo parses `name = "text";` entries, decodes escapes with the pool's builder, shows that equal text gets
// equal ids, and prints the result after the source buffer is gone (the ids do not point into it).

#include <cstdio>
#include <memory>
#include <string>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/string_pool.hpp>
#include <microfmt/parsing/token_stream.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::parse_error;
using microfmt::string_id;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

// 64 bytes of text live inside the pool object; more spill to the heap, up to the limits given at construction.
using pool_type = microfmt::string_pool<64>;

// A tree node would hold these two ids instead of two string_views into the source.
struct RELOCO_COPYABLE entry {
  string_id key{};
  string_id value{};
};

// Turns the raw text of a string token (quotes and escapes included) into an interned string. The builder writes
// straight into the pool, so no temporary buffer is needed; if the literal is already known the copy is dropped.
reloco::expected<string_id, parse_error> decode(pool_type &pool, const microfmt::token &literal) {
  auto builder = pool.begin();
  const string_view body = literal.text.substr(1, literal.text.size() - 2); // without the quotes
  for (size_t i = 0; i < body.size(); ++i) {
    if (body[i] != '\\') {
      builder.push_back(body[i]);
      continue;
    }
    ++i;
    if (i >= body.size()) {
      return reloco::unexpected<parse_error>(parse_error{"dangling escape", literal});
    }
    switch (body[i]) {
    case 'n':
      builder.push_back('\n');
      break;
    case 't':
      builder.push_back('\t');
      break;
    case '"':
    case '\\':
      builder.push_back(body[i]);
      break;
    default: // the builder is abandoned by its destructor, so the pool keeps nothing from this literal
      return reloco::unexpected<parse_error>(parse_error{"unknown escape", literal});
    }
  }
  return builder.finish(literal);
}

// entry := NAME "=" STRING ";"
reloco::expected<entry, parse_error> parse_entry(token_stream &ts, pool_type &pool) {
  auto name = ts.expect(token_kind::identifier, "expected a name");
  if (!name) {
    return reloco::unexpected<parse_error>(name.error());
  }
  if (auto eq = ts.expect(token_kind::op, "=", "expected '='"); !eq) {
    return reloco::unexpected<parse_error>(eq.error());
  }
  auto literal = ts.expect(token_kind::string, "expected a string");
  if (!literal) {
    return reloco::unexpected<parse_error>(literal.error());
  }
  if (auto semi = ts.expect(token_kind::punctuation, ";", "expected ';'"); !semi) {
    return reloco::unexpected<parse_error>(semi.error());
  }
  auto key = pool.intern(name.value()); // equal names get equal ids
  if (!key) {
    return reloco::unexpected<parse_error>(key.error());
  }
  auto value = decode(pool, literal.value());
  if (!value) {
    return reloco::unexpected<parse_error>(value.error());
  }
  return entry{key.value(), value.value()};
}

constexpr string_view kOperators[] = {"="};

} // namespace

int main() {
  microfmt::lexer_rules rules;
  rules.operators = kOperators;

  pool_type pool;
  reloco::sso_vector<entry, 8> entries;
  microfmt::buffer_sink<1024> out;

  // 1. The source lives in a heap buffer that is freed before the entries are printed.
  {
    auto source = std::make_unique<std::string>("greeting = \"hello\";\n"
                                                "title = \"say \\\"hi\\\"\\t!\";\n"
                                                "greeting = \"hello\";\n");
    token_stream ts{string_view{source->data(), source->size()}, rules};
    while (!ts.at_end()) {
      auto parsed = parse_entry(ts, pool);
      if (!parsed || !entries.try_push_back(parsed.value())) {
        break;
      }
    }
  }

  for (size_t i = 0; i < entries.size(); ++i) {
    microfmt::format_to(out.as_sink(), "{} = [{}]\n", pool.view(entries[i].key), pool.view(entries[i].value));
  }
  // Entries 0 and 2 are the same name with the same value, so the ids are equal: no byte comparison needed.
  microfmt::format_to(out.as_sink(), "same key: {}, same value: {}\n", entries[0].key == entries[2].key,
                      entries[0].value == entries[2].value);
  microfmt::format_to(out.as_sink(), "{} entries, {} distinct strings, {} bytes\n", entries.size(), pool.size(),
                      pool.bytes());

  // 2. A bad escape is an ordinary parse error with a position. The name `bad` was already interned, but the
  // half-decoded literal left no bytes behind.
  {
    constexpr string_view input = "bad = \"oops \\q\";";
    token_stream ts{input, rules};
    const size_t before = pool.bytes();
    auto parsed = parse_entry(ts, pool);
    if (!parsed) {
      microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(parsed.error(), input));
    }
    microfmt::format_to(out.as_sink(), "bytes added by the failed entry: {} (just the name)\n", pool.bytes() - before);
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
