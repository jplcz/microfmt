// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Operators chosen at run time: an operator_table is filled with named precedence levels, evaluates the same text,
// then gets a new level inserted between two existing ones, which changes how the text groups.

#include <cstdint>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/operator_table.hpp>
#include <microfmt/sinks/stdio.hpp>

namespace {

using table_type = microfmt::operator_table<8>;
using value_result = reloco::expected<int64_t, microfmt::parse_error>;

// The callbacks parse_expression calls: leaves become numbers, operators combine two numbers right away.
struct evaluator {
  value_result primary(microfmt::token_stream &s) const {
    auto number = s.expect(microfmt::token_kind::integer, "expected a number");
    if (!number) {
      return reloco::unexpected<microfmt::parse_error>(number.error());
    }
    return microfmt::parse<int64_t>(number.value().text).value();
  }

  value_result make_binary(const microfmt::binary_op &op, int64_t l, int64_t r) const {
    if (op.text == "+") {
      return l + r;
    }
    if (op.text == "*") {
      return l * r;
    }
    return l - r; // "-" here, and the made-up "<+>" below
  }
};

int64_t evaluate(const table_type &table, microfmt::string_view text) {
  // The lexer must emit every declared spelling as an `op` token; the table knows them all.
  microfmt::lexer_rules rules;
  rules.operators = table.spellings();
  microfmt::token_stream ts{text, rules};
  const evaluator actions;
  auto result = microfmt::parse_expression<int64_t>(ts, table.grammar(), actions);
  return result ? result.value() : -1;
}

} // namespace

int main() {
  table_type table;

  // Levels are named by their order, not by numbers: add_level() makes one tighter than all existing levels.
  const auto sum = table.add_level().value();
  const auto product = table.add_level().value();
  (void)table.add_binary("+", sum);
  (void)table.add_binary("-", sum);
  (void)table.add_binary("*", product);

  microfmt::println("1 + 2 * 3 = {}", evaluate(table, "1 + 2 * 3"));

  // A late declaration: "<+>" is slotted between `sum` and `product`, with no number to choose and no
  // existing precedence to shift. It subtracts here, so 10 <+> 2 * 3 == 10 - 6 and 2 * 3 <+> 1 == 6 - 1.
  const auto between = table.add_level_above(sum).value();
  (void)table.add_binary("<+>", between);
  microfmt::println("10 <+> 2 * 3 = {}", evaluate(table, "10 <+> 2 * 3"));
  microfmt::println("1 + 10 <+> 2 = {}", evaluate(table, "1 + 10 <+> 2"));

  // Declaring the same spelling twice is refused with a static message.
  const auto again = table.add_binary("+", sum);
  microfmt::println("again: {}", again.error());

  return 0;
}
