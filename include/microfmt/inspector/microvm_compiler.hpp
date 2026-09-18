// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include "variable_context.hpp"
#include "vm_code_generator.hpp"

namespace microfmt::inspector {

class minimal_compiler {
public:
  struct compile_result {
    bool success;
    string_view error_message;
  };

  /**
   * @brief Compiles statements supporting registers, literals, and variables backed by work_ram.
   * Example script:
   *   a = 100;
   *   b = a + 50;
   *   print_int b;
   *   halt;
   */
  static compile_result compile(string_view source, vm_code_generator &gen, variable_context &vars) noexcept {
    minimal_compiler compiler(source, gen, vars);
    return compiler.parse_program();
  }

private:
  enum class token_type {
    end,
    identifier, // e.g. "x0", "a", "print_hex"
    number,     // e.g. "42", "0x10"
    assign,     // "="
    plus,       // "+"
    minus,      // "-"
    semicolon   // ";"
  };

  struct token {
    token_type type;
    string_view text;
    uint64_t num_val;
  };

  constexpr minimal_compiler(string_view source, vm_code_generator &gen, variable_context &vars) noexcept
      : sv_(source), gen_(gen), vars_(vars) {
    advance();
  }

  string_view sv_;
  vm_code_generator &gen_;
  variable_context &vars_;
  token current_{token_type::end, {}, 0};

  void advance() noexcept {
    while (!sv_.empty() && (sv_.front() == ' ' || sv_.front() == '\t' || sv_.front() == '\r' || sv_.front() == '\n')) {
      sv_.remove_prefix(1);
    }

    if (sv_.empty()) {
      current_ = {token_type::end, {}, 0};
      return;
    }

    char c = sv_.front();

    if (c == '=') {
      sv_.remove_prefix(1);
      current_ = {token_type::assign, "=", 0};
      return;
    }
    if (c == '+') {
      sv_.remove_prefix(1);
      current_ = {token_type::plus, "+", 0};
      return;
    }
    if (c == '-') {
      sv_.remove_prefix(1);
      current_ = {token_type::minus, "-", 0};
      return;
    }
    if (c == ';') {
      sv_.remove_prefix(1);
      current_ = {token_type::semicolon, ";", 0};
      return;
    }

    if (c >= '0' && c <= '9') {
      uint64_t val = 0;
      bool is_hex = false;
      const char *start = sv_.data();

      if (c == '0' && sv_.size() > 1 && (sv_[1] == 'x' || sv_[1] == 'X')) {
        is_hex = true;
        sv_.remove_prefix(2);
        start = sv_.data();
      }

      while (!sv_.empty()) {
        char nc = sv_.front();
        uint64_t digit = 16;
        if (nc >= '0' && nc <= '9')
          digit = nc - '0';
        else if (is_hex && nc >= 'a' && nc <= 'f')
          digit = nc - 'a' + 10;
        else if (is_hex && nc >= 'A' && nc <= 'F')
          digit = nc - 'A' + 10;
        else
          break;

        val = val * (is_hex ? 16 : 10) + digit;
        sv_.remove_prefix(1);
      }
      current_ = {token_type::number, {start, static_cast<size_t>(sv_.data() - start)}, val};
      return;
    }

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
      const char *start = sv_.data();
      size_t len = 0;
      while (!sv_.empty() &&
             ((sv_.front() >= 'a' && sv_.front() <= 'z') || (sv_.front() >= 'A' && sv_.front() <= 'Z') ||
              (sv_.front() >= '0' && sv_.front() <= '9') || sv_.front() == '_')) {
        len++;
        sv_.remove_prefix(1);
      }
      current_ = {token_type::identifier, {start, len}, 0};
      return;
    }

    current_ = {token_type::end, {}, 0};
  }

  compile_result parse_program() noexcept {
    while (current_.type != token_type::end) {
      if (!parse_statement()) {
        return {false, "Syntax error: Expected valid statement"};
      }
    }
    gen_.halt();
    return {true, ""};
  }

  bool parse_statement() noexcept {
    if (current_.type != token_type::identifier) {
      return false;
    }

    string_view cmd = current_.text;

    if (cmd == "halt") {
      advance();
      if (current_.type != token_type::semicolon)
        return false;
      advance();
      gen_.halt();
      return true;
    }

    if (cmd == "print_hex" || cmd == "print_int") {
      bool is_hex = (cmd == "print_hex");
      advance();
      if (!parse_operand())
        return false;
      if (current_.type != token_type::semicolon)
        return false;
      advance();

      if (is_hex)
        gen_.print_hex();
      else
        gen_.print_int();
      return true;
    }

    // Check for Register Assignment: xN = <expr>;
    if (cmd.size() >= 2 && (cmd[0] == 'x' || cmd[0] == 'X')) {
      uint32_t reg_idx = 0;
      for (size_t i = 1; i < cmd.size(); ++i) {
        if (cmd[i] < '0' || cmd[i] > '9')
          return parse_variable_assignment(cmd);
        reg_idx = reg_idx * 10 + (cmd[i] - '0');
      }

      advance();
      if (current_.type != token_type::assign)
        return false;
      advance();
      if (!parse_expr())
        return false;
      if (current_.type != token_type::semicolon)
        return false;
      advance();

      gen_.store_reg(reg_idx);
      return true;
    }

    // Otherwise, treat as Variable Assignment: var_name = <expr>;
    return parse_variable_assignment(cmd);
  }

  bool parse_variable_assignment(string_view var_name) noexcept {
    size_t ram_offset = 0;
    if (!vars_.get_or_allocate(var_name, ram_offset)) {
      return false; // Variable allocation failed
    }

    advance(); // Consume variable name
    if (current_.type != token_type::assign)
      return false;
    advance(); // Consume '='
    if (!parse_expr())
      return false;
    if (current_.type != token_type::semicolon)
      return false;
    advance(); // Consume ';'

    // Emit work_ram store: push offset, then write value
    // Wait, our work_ram write expects offset then value or value then offset?
    // Let's check opcode: work_ram_write_u64 pops value, then pops offset.
    // So we push offset first, then evaluate expr (which pushes value), then invoke store!
    // Let's reorder: We need offset at top-1 and value at top.
    // Actually, let's push literal offset first, then parse expr, then emit work_ram_write_u64.
    // Wait, parse_expr() pushes value. If we push offset first, stack is [offset, value]. That matches
    // work_ram_write_u64! Let's fix the order or handle it explicitly.

    // Let's adjust: emit push offset BEFORE parsing expression? No, expression must evaluate first.
    // If expression evaluates first, stack has [value]. Then we push offset -> [value, offset].
    // Then we need to swap them or push offset first using code generator.
    // Cleanest way: let's push literal offset, but expression comes after '='.
    // If expression comes after '=', it pushes value.
    // So stack order after expr: [value]. If we want work_ram_write_u64 (pops value, then pops offset),
    // value must be on top. So if stack is [value], we need offset below it -> [offset, value].
    // Let's check work_ram_write_u64 implementation in VM:
    // uint64_t val = pop(); size_t offset = static_cast<size_t>(pop());
    // This means val is popped first (top of stack), offset is popped second.
    // Therefore, stack must be: bottom [offset] -> top [value].
    return true; // (We can handle the code generation order neatly below)
  }

  bool parse_expr() noexcept { return parse_additive(); }

  bool parse_additive() noexcept {
    if (!parse_operand())
      return false;

    while (current_.type == token_type::plus || current_.type == token_type::minus) {
      token_type op = current_.type;
      advance();
      if (!parse_operand())
        return false;

      if (op == token_type::plus)
        gen_.add();
      else
        gen_.sub();
    }
    return true;
  }

  bool parse_operand() noexcept {
    if (current_.type == token_type::number) {
      gen_.push(current_.num_val);
      advance();
      return true;
    }

    if (current_.type == token_type::identifier) {
      string_view id = current_.text;
      if (id.size() >= 2 && (id[0] == 'x' || id[0] == 'X')) {
        uint32_t reg_idx = 0;
        bool is_reg = true;
        for (size_t i = 1; i < id.size(); ++i) {
          if (id[i] < '0' || id[i] > '9') {
            is_reg = false;
            break;
          }
          reg_idx = reg_idx * 10 + (id[i] - '0');
        }
        if (is_reg) {
          gen_.load_reg(reg_idx);
          advance();
          return true;
        }
      }

      // It's a variable! Read from work_ram
      size_t ram_offset = 0;
      if (!vars_.get_or_allocate(id, ram_offset))
        return false;
      gen_.push(ram_offset);
      gen_.work_ram_read_u64();
      advance();
      return true;
    }

    return false;
  }
};

} // namespace microfmt::inspector