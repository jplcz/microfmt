// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "../microfmt.hpp"
#include "../reloco.hpp"
#include "dwarf_abi.hpp"
#include "register_context.hpp" // Assuming the provided snippet is here
#include <cstdint>

namespace microfmt::gdb {

/**
 * @brief Encodes register values from a execution context into GDB RSP hex strings.
 *
 * Generates payloads for 'g' (read all general registers) and 'p' (read single register) requests.
 */
class MICROFMT_API_CLASS register_array_encoder {
public:
  /**
   * @brief Encodes the full register layout for a specific architecture ('g' packet response).
   *
   * Iterates over the standard GDB layout in sequential order, querying the context
   * using the mapped DWARF indices.
   *
   * @tparam AbiTraits The target ABI traits (e.g., aarch64_abi_traits).
   * @param out The sink to stream the hex payload into.
   * @param ctx The typed-erased kernel/debugger thread context.
   */
  template <typename AbiTraits> static void encode_all_registers(sink out, const register_context_ref &ctx) noexcept {
    if (ctx.is_null()) {
      return;
    }

    for (const auto &reg : AbiTraits::gdb_register_traits::layout()) {
      encode_single_register(out, ctx, reg);
    }
  }

  /**
   * @brief Encodes a single register based on its mapping ('p' packet response).
   *
   * @param out The sink to stream the hex payload into.
   * @param ctx The typed-erased kernel/debugger thread context.
   * @param reg The register mapping describing size and DWARF index.
   */
  static void encode_single_register(sink out, const register_context_ref &ctx, const register_mapping &reg) noexcept {
    if (ctx.is_null()) {
      emit_unavailable(out, reg.bit_size / 8);
      return;
    }

    const size_t byte_size = reg.bit_size / 8;
    if (byte_size == 0)
      return;

    // Fast path: Most registers (including 512-bit AVX vectors) fit in 64 bytes.
    // We use a stack buffer to avoid touching the scratch allocator for routine lookups.
    if (byte_size <= 64) {
      uint8_t stack_buf[64];
      if (ctx.read_raw(reg.dwarf_index, stack_buf, byte_size)) {
        emit_hex_bytes(out, span<const uint8_t>{stack_buf, byte_size});
      } else {
        emit_unavailable(out, byte_size); // Context refused to read (e.g., power-gated core)
      }
    }
    // Slow path: Scalable Vector Extensions (SVE) can exceed 64 bytes.
    // Fall back to the execution context's attached scratch memory.
    else {
      auto scratch = ctx.scratch();
      if (byte_size <= scratch.size()) {
        if (ctx.read_raw(reg.dwarf_index, scratch.data(), byte_size)) {
          emit_hex_bytes(out, span<const uint8_t>{reinterpret_cast<const uint8_t *>(scratch.data()), byte_size});
        } else {
          emit_unavailable(out, byte_size);
        }
      } else {
        // Context scratch buffer is too small to hold this register.
        emit_unavailable(out, byte_size);
      }
    }
  }

private:
  /**
   * @brief Formats raw memory bytes into target byte-order hex (suitable for GDB).
   */
  static void emit_hex_bytes(sink target, span<const uint8_t> data) noexcept {
    for (uint8_t b : data) {
      target.put(microfmt::detail::hex_digits_lower[(b >> 4) & 0x0F]);
      target.put(microfmt::detail::hex_digits_lower[b & 0x0F]);
    }
  }

  /**
   * @brief Emits 'x' characters for a register whose value is unavailable.
   *
   * GDB interprets 'x' in a register stream as "value not available in this context",
   * preventing it from displaying garbage data.
   */
  static void emit_unavailable(sink target, size_t byte_size) noexcept {
    for (size_t i = 0; i < byte_size * 2; ++i) {
      target.put('x');
    }
  }
};

/**
 * @brief Decodes GDB RSP hex strings into binary values and writes them to the execution context.
 *
 * Processes payloads for 'G' (write all general registers) and 'P' (write single register) requests.
 */
class MICROFMT_API_CLASS register_array_decoder {
public:
  /**
   * @brief Decodes the full register layout hex payload ('G' packet payload).
   *
   * Iterates over the standard GDB layout in sequential order, extracting
   * hex chunks, decoding them, and pushing them to the target context.
   *
   * @tparam AbiTraits The target ABI traits (e.g., aarch64_abi_traits).
   * @param hex_payload The raw hex string received in the 'G' packet.
   * @param ctx The typed-erased kernel/debugger thread context.
   * @return true if successfully decoded and written, false if the payload is malformed.
   */
  template <typename AbiTraits>
  [[nodiscard]] static bool decode_all_registers(string_view hex_payload, const register_context_ref &ctx) noexcept {
    if (ctx.is_null()) {
      return false;
    }

    size_t offset = 0;
    for (const auto &reg : AbiTraits::gdb_register_traits::layout()) {
      const size_t hex_len = reg.bit_size / 4; // 2 hex chars per byte

      // GDB sometimes sends truncated 'G' packets containing only core registers.
      // If we run out of hex payload, we stop gracefully.
      if (offset + hex_len > hex_payload.size()) {
        break;
      }

      string_view reg_hex = hex_payload.substr(offset, hex_len);
      offset += hex_len;

      // If the register data starts with 'x', GDB is explicitly telling us
      // this register is unavailable/ignored. Skip writing it.
      if (!reg_hex.empty() && (reg_hex[0] == 'x' || reg_hex[0] == 'X')) {
        continue;
      }

      if (!decode_single_register(reg_hex, ctx, reg)) {
        return false; // Stop on first decoding/write failure
      }
    }

    return true;
  }

  /**
   * @brief Decodes a single register hex payload ('P' packet value).
   *
   * @param hex_payload The hex value string to decode.
   * @param ctx The typed-erased kernel/debugger thread context.
   * @param reg The register mapping describing size and DWARF index.
   * @return true if successfully parsed and written, false otherwise.
   */
  [[nodiscard]] static bool decode_single_register(string_view hex_payload, const register_context_ref &ctx,
                                                   const register_mapping &reg) noexcept {
    if (ctx.is_null())
      return false;

    const size_t byte_size = reg.bit_size / 8;
    if (byte_size == 0)
      return true;

    if (hex_payload.size() < byte_size * 2) {
      return false; // Hex string is too short for this register
    }

    // Ignore 'x' masked registers
    if (hex_payload[0] == 'x' || hex_payload[0] == 'X') {
      return true;
    }

    // Fast path: Use stack buffer for registers up to 64 bytes (AVX-512)
    if (byte_size <= 64) {
      uint8_t stack_buf[64];
      auto out_span = span<uint8_t>{stack_buf, byte_size};

      if (!parse_hex_to_bytes(hex_payload.substr(0, byte_size * 2), out_span)) {
        return false;
      }
      return ctx.write_raw(reg.dwarf_index, stack_buf, byte_size);
    }
    // Slow path: Fallback to context scratch buffer for massive vectors (SVE)
    else {
      auto scratch = ctx.scratch();
      if (byte_size <= scratch.size()) {
        auto out_span = span<uint8_t>{reinterpret_cast<uint8_t *>(scratch.data()), byte_size};

        if (!parse_hex_to_bytes(hex_payload.substr(0, byte_size * 2), out_span)) {
          return false;
        }
        return ctx.write_raw(reg.dwarf_index, scratch.data(), byte_size);
      }
      return false; // Context scratch buffer too small
    }
  }

private:
  /**
   * @brief Safely parses a hex string into a binary byte span.
   */
  [[nodiscard]] static bool parse_hex_to_bytes(string_view hex, span<uint8_t> out) noexcept {
    for (size_t i = 0; i < out.size(); ++i) {
      uint8_t hi = hex_char_to_val(hex[i * 2]);
      uint8_t lo = hex_char_to_val(hex[i * 2 + 1]);

      if (hi == 0xFF || lo == 0xFF) {
        return false; // Invalid hex character encountered
      }
      out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
  }

  /**
   * @brief Converts a single hex character to its integer value.
   */
  [[nodiscard]] static constexpr uint8_t hex_char_to_val(char c) noexcept {
    if (c >= '0' && c <= '9')
      return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
      return static_cast<uint8_t>(c - 'A' + 10);
    return 0xFF; // Error marker
  }
};

} // namespace microfmt::gdb