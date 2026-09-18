// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include "gdb_registers.hpp"
#include "variable_context.hpp"
#include "vm_code_gen.hpp"
#include "vm_lexer.hpp"

namespace microfmt::inspector {

struct target_arch_traits {
  bool (*lookup_register)(string_view name, uint32_t &out_dwarf_index) noexcept;

  template <typename ArchTag> [[nodiscard]] static constexpr target_arch_traits create() noexcept {
    return target_arch_traits{[](string_view name, uint32_t &out_dwarf_index) noexcept -> bool {
      using traits = gdb::register_traits<ArchTag>;
      if (auto *reg = traits::find_by_name(name)) {
        out_dwarf_index = reg->dwarf_index;
        return true;
      }
      return false;
    }};
  }
};

class vm_compiler {
public:
  struct compile_result {
    bool success;
  };

  /**
   * @brief Compiles scripts using target architecture traits and streams diagnostics to a sink.
   * @param source Script text source string_view.
   * @param gen Code generator bytecode output buffer.
   * @param vars Variable context for work_ram slots.
   * @param arch Target architecture register traits.
   * @param error_sink Sink for streaming compilation errors.
   */
  static compile_result compile(string_view source, vm_code_generator &gen, variable_context &vars,
                                target_arch_traits arch, sink error_sink) noexcept {
    vm_lexer lexer(source);
    vm_compiler compiler(lexer, gen, vars, arch, error_sink);
    return compiler.parse_program();
  }

private:
  constexpr vm_compiler(vm_lexer &lexer, vm_code_generator &gen, variable_context &vars, target_arch_traits arch,
                        sink error_sink) noexcept
      : lexer_(lexer), gen_(gen), vars_(vars), arch_(arch), error_sink_(error_sink) {}

  vm_lexer &lexer_;
  vm_code_generator &gen_;
  variable_context &vars_;
  target_arch_traits arch_;
  sink error_sink_;

  void report_error(string_view message) noexcept { format_to(error_sink_, "[COMPILER ERROR] {}\n", message); }

  compile_result parse_program() noexcept {
    while (lexer_.current().type != token_type::end) {
      if (!parse_statement()) {
        report_error("Syntax error: Expected valid statement");
        return {false};
      }
    }
    gen_.halt();
    return {true};
  }

  bool parse_statement() noexcept {
    const auto &tok = lexer_.current();
    if (tok.type != token_type::identifier) {
      return false;
    }

    string_view cmd = tok.text;

    if (cmd == "halt") {
      lexer_.advance();
      if (lexer_.current().type != token_type::semicolon) {
        report_error("Expected ';' after halt");
        return false;
      }
      lexer_.advance();
      gen_.halt();
      return true;
    }

    if (cmd == "print_hex" || cmd == "print_int") {
      bool is_hex = (cmd == "print_hex");
      lexer_.advance();
      if (!parse_operand()) {
        report_error("Expected valid operand after print statement");
        return false;
      }
      if (lexer_.current().type != token_type::semicolon) {
        report_error("Expected ';' at end of print statement");
        return false;
      }
      lexer_.advance();

      if (is_hex)
        gen_.print_hex();
      else
        gen_.print_int();
      return true;
    }

    // Variable declaration: let var_name = expr;
    if (cmd == "let") {
      lexer_.advance();
      if (lexer_.current().type != token_type::identifier) {
        report_error("Expected variable identifier after 'let'");
        return false;
      }
      string_view var_name = lexer_.current().text;

      size_t ram_offset = 0;
      if (!vars_.get_or_allocate(var_name, ram_offset)) {
        report_error("Work RAM or symbol table overflow during variable allocation");
        return false;
      }

      lexer_.advance();
      if (lexer_.current().type != token_type::assign) {
        report_error("Expected '=' in variable declaration");
        return false;
      }
      lexer_.advance();

      gen_.push(ram_offset);
      if (!parse_expr()) {
        report_error("Invalid expression in variable initialization");
        return false;
      }
      if (lexer_.current().type != token_type::semicolon) {
        report_error("Expected ';' at end of variable declaration");
        return false;
      }
      lexer_.advance();

      gen_.work_ram_write_u64();
      return true;
    }

    // Check if identifier is a target register
    uint32_t dwarf_idx = 0;
    if (arch_.lookup_register && arch_.lookup_register(cmd, dwarf_idx)) {
      lexer_.advance();
      if (lexer_.current().type != token_type::assign) {
        report_error("Expected '=' after register assignment target");
        return false;
      }
      lexer_.advance();
      if (!parse_expr()) {
        report_error("Invalid expression for register assignment");
        return false;
      }
      if (lexer_.current().type != token_type::semicolon) {
        report_error("Expected ';' at end of register assignment");
        return false;
      }
      lexer_.advance();

      gen_.store_reg(dwarf_idx);
      return true;
    }

    // Otherwise, existing variable assignment: var_name = expr;
    size_t ram_offset = 0;
    if (!vars_.get_or_allocate(cmd, ram_offset)) {
      report_error("Undefined variable or symbol table overflow");
      return false;
    }

    lexer_.advance();
    if (lexer_.current().type != token_type::assign) {
      report_error("Expected '=' after variable assignment target");
      return false;
    }
    lexer_.advance();

    gen_.push(ram_offset);
    if (!parse_expr()) {
      report_error("Invalid expression for variable assignment");
      return false;
    }
    if (lexer_.current().type != token_type::semicolon) {
      report_error("Expected ';' at end of variable assignment");
      return false;
    }
    lexer_.advance();

    gen_.work_ram_write_u64();
    return true;
  }

  bool parse_expr() noexcept { return parse_additive(); }

  bool parse_additive() noexcept {
    if (!parse_operand())
      return false;

    while (lexer_.current().type == token_type::plus || lexer_.current().type == token_type::minus) {
      token_type op = lexer_.current().type;
      lexer_.advance();
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
    const auto &tok = lexer_.current();

    if (tok.type == token_type::number) {
      gen_.push(tok.num_val);
      lexer_.advance();
      return true;
    }

    if (tok.type == token_type::identifier) {
      string_view id = tok.text;

      // Check register via wrapper
      uint32_t dwarf_idx = 0;
      if (arch_.lookup_register && arch_.lookup_register(id, dwarf_idx)) {
        gen_.load_reg(dwarf_idx);
        lexer_.advance();
        return true;
      }

      // Variable load from work_ram
      size_t ram_offset = 0;
      if (!vars_.get_or_allocate(id, ram_offset))
        return false;
      gen_.push(ram_offset);
      gen_.work_ram_read_u64();
      lexer_.advance();
      return true;
    }

    return false;
  }
};

} // namespace microfmt::inspector