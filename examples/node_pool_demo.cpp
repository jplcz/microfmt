// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

// Building a syntax tree without pointers: every node is stored in a node_pool and linked by handle.
// The demo parses `1 + 2 * 3`, prints the tree as an S-expression, shows that `pooled` takes the nodes of a
// failed alternative back, and reports a full pool as an ordinary parse error.

#include <cstdio>
#include <string>

#include <microfmt/microfmt.hpp>
#include <microfmt/parsing/ast_pool.hpp>
#include <microfmt/parsing/number_parse.hpp>
#include <microfmt/parsing/token_stream.hpp>
#include <microfmt/parsing/token_stream_formatters.hpp>

namespace {

using microfmt::node_id;
using microfmt::parse_error;
using microfmt::string_view;
using microfmt::token_kind;
using microfmt::token_stream;

// A node is a number (op == 0) or a binary operation whose children are handles, not pointers.
struct RELOCO_POINTER node {
  int64_t value{0};
  char op{0};
  microfmt::token at{};
  node_id<node> left{};
  node_id<node> right{};
};

using handle = node_id<node>;
using tree_result = reloco::expected<handle, parse_error>;
// 8 nodes live inside the pool object; bigger trees spill to the heap, up to the limit given at construction.
using pool_type = microfmt::node_pool<node, 8>;

constexpr string_view kOperators[] = {"+", "*"};
constexpr microfmt::binary_op kBinary[] = {{"+", 10, microfmt::assoc::left}, {"*", 20, microfmt::assoc::left}};

// The callbacks parse_expression calls: leaves and operators become nodes.
struct tree_actions {
  pool_type &pool;

  tree_result primary(token_stream &s) const {
    auto number = s.expect(token_kind::integer, "expected a number");
    if (!number) {
      return reloco::unexpected<parse_error>(number.error());
    }
    return pool.make(number.value(),
                     node{microfmt::parse<int64_t>(number.value().text).value(), 0, number.value(), {}, {}});
  }

  tree_result make_binary(const microfmt::binary_op &op, handle l, handle r) const {
    const microfmt::token at = pool.get(l)->at;
    return pool.make(at, node{0, op.text[0], at, l, r});
  }
};

tree_result parse_sum(token_stream &ts, pool_type &pool) {
  tree_actions actions{pool};
  const microfmt::expression_grammar grammar{kBinary, {}, {}, {}, {}};
  return microfmt::parse_expression<handle>(ts, grammar, actions);
}

void print_tree(const pool_type &pool, handle id, std::string &out) {
  const auto ref = pool.get(id);
  const node &n = *ref;
  if (n.op == 0) {
    out += std::to_string(n.value);
    return;
  }
  out += '(';
  out += n.op;
  out += ' ';
  print_tree(pool, n.left, out);
  out += ' ';
  print_tree(pool, n.right, out);
  out += ')';
}

} // namespace

int main() {
  microfmt::lexer_rules rules;
  rules.operators = kOperators;

  microfmt::buffer_sink<1024> out;

  // 1. Parse and print: precedence shows in the shape of the tree.
  {
    pool_type pool;
    token_stream ts{"1 + 2 * 3", rules};
    const tree_result tree = parse_sum(ts, pool);
    std::string text;
    print_tree(pool, tree.value(), text);
    microfmt::format_to(out.as_sink(), "tree: {}  ({} nodes)\n", string_view{text.data(), text.size()}, pool.size());
  }

  // 2. Backtracking: the first alternative parses the whole sum, then fails on the missing '!'. `pooled` rolls
  // the pool back, so only the nodes of the second alternative stay.
  {
    pool_type pool;
    token_stream ts{"1 + 2 * 3;", rules};
    auto with_bang = microfmt::pooled(pool, [&pool](token_stream &s) -> tree_result {
      auto tree = parse_sum(s, pool);
      if (!tree) {
        return tree;
      }
      if (auto bang = s.expect(token_kind::punctuation, "!", "expected '!'"); !bang) {
        return reloco::unexpected<parse_error>(bang.error());
      }
      return tree;
    });
    auto with_semicolon = microfmt::pooled(pool, [&pool](token_stream &s) -> tree_result {
      auto tree = parse_sum(s, pool);
      if (tree && !s.expect(token_kind::punctuation, ";", "expected ';'")) {
        return reloco::unexpected<parse_error>(s.error_here("expected ';'"));
      }
      return tree;
    });
    const tree_result tree = microfmt::parse_choice(ts, with_bang, with_semicolon);
    microfmt::format_to(out.as_sink(), "after backtracking: {} nodes (not 10)\n", pool.size());
    (void)tree;
  }

  // 3. A pool limited to 4 nodes: the fifth `make` is a normal error with a line and column.
  {
    pool_type pool{4};
    constexpr string_view input = "1 + 2 * 3";
    token_stream ts{input, rules};
    const tree_result tree = parse_sum(ts, pool);
    if (!tree) {
      microfmt::format_to(out.as_sink(), "{}\n", microfmt::report(tree.error(), input));
    }
  }

  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  std::printf("%.*s", static_cast<int>(out.view().size()), out.view().data());

  RELOCO_END_UNSAFE_BUFFER_USAGE;
}
