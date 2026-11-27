// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file elf_format.hpp
 * @brief Constants, input descriptions and a forward-only byte emitter for ELF core files.
 *
 * Why: an ELF (Executable and Linking Format) core file is what `gdb` and
 * `lldb` open to inspect a crashed program. Writing one normally means
 * building structures in memory and seeking back to patch sizes. This header
 * provides the small pieces the streaming writer needs instead: the numeric
 * constants, plain descriptions of the data a dump is made of, and an emitter
 * that writes integers of a fixed size and byte order straight into a sink, one
 * field at a time, so no memory is allocated and no struct is overlaid on raw
 * bytes.
 */

#include "../inspector/register_context.hpp"
#include "../microfmt.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace microfmt::coredump {

/** @name ELF constants */
///@{
inline constexpr std::uint16_t et_core = 4;
inline constexpr std::uint32_t pt_load = 1;
inline constexpr std::uint32_t pt_note = 4;
inline constexpr std::uint32_t pf_x = 1;
inline constexpr std::uint32_t pf_w = 2;
inline constexpr std::uint32_t pf_r = 4;
inline constexpr std::uint32_t nt_prstatus = 1;
inline constexpr std::uint32_t nt_prfpreg = 2;
inline constexpr std::uint32_t nt_auxv = 6;
inline constexpr std::uint32_t nt_arm_vfp = 0x400;
inline constexpr std::uint32_t nt_prxfpreg = 0x46e62b7f;
inline constexpr std::uint32_t nt_file = 0x46494c45; // "FILE"
///@}

/**
 * @brief Describes one per-thread *register set*: all registers of one kind, laid out exactly as the note stores them.
 *
 * An architecture policy lists its sets as constants, so the plan pass knows every size without touching the
 * target. The writer fetches the bytes of each set from the thread's register context with
 * `read_raw(regset_index(type), buffer, size)`.
 */
struct RELOCO_POINTER core_regset {
  std::uint32_t type{0}; ///< Note type (`NT_*`), also the key used to fetch the set.
  string_view name;      ///< Note owner name, "CORE" or "LINUX".
  std::uint32_t size{0}; ///< Exact size of the blob in bytes.
};

/** @brief Register index reserved for fetching register sets; far above any real DWARF register number. */
inline constexpr std::uint32_t regset_index_base = 0x10000;

/** @brief The register-context index under which the register set of note type @p note_type is read. */
constexpr std::uint32_t regset_index(std::uint32_t note_type) noexcept { return regset_index_base + note_type; }

/** @brief A contiguous range of target memory that should be part of the dump. */
struct core_region {
  std::uint64_t address{0};
  std::uint64_t size{0};
  std::uint32_t flags{pf_r}; ///< Combination of `pf_r`, `pf_w`, `pf_x`.
};

/** @brief A thread (or CPU context) at the time of the dump. */
struct RELOCO_POINTER core_thread {
  std::uint32_t id{0};
  std::int32_t signal{0}; ///< Signal or fault number that stopped the thread; 0 if none.
  register_context_ref regs{};
};

/** @brief An address range backed by a file, reported to the debugger through the `NT_FILE` note. */
struct RELOCO_POINTER core_mapped_file {
  std::uint64_t start{0};
  std::uint64_t end{0};
  std::uint64_t offset{0}; ///< Offset of `start` inside the file, in bytes (a multiple of the page size).
  string_view path{};
};

/** @brief One entry of the ELF auxiliary vector (`AT_*` type and its value), as passed by the kernel to a process. */
struct core_auxv_entry {
  std::uint64_t type{0};
  std::uint64_t value{0};
};

/** @brief Result of the planning pass: everything the headers need, in a few integers. */
struct core_plan {
  bool valid{false};
  std::uint32_t load_segments{0};
  std::uint32_t threads{0};
  std::uint32_t files{0};
  std::uint32_t auxv_entries{0};  ///< Auxiliary vector entries, not counting the terminator.
  std::uint64_t path_bytes{0};    ///< Total length of all mapped-file paths including their terminating NULs.
  std::uint64_t note_bytes{0};    ///< Size of the whole note segment.
  std::uint64_t segment_bytes{0}; ///< Memory content to be written.
  std::uint64_t total_size{0};    ///< Exact size of the finished file.
};

/** @brief What happened while writing; all zero/false means the file is exactly as planned. */
struct core_stats {
  std::uint64_t bytes_written{0};
  std::uint32_t read_failures{0}; ///< Memory reads that failed in the emit pass (zeros were written instead).
  bool plan_mismatch{false};      ///< The input changed between the passes; the file was padded to keep its layout.
};

namespace detail {

/** @brief Writes fixed-width integers in the byte order @p Little to a sink and counts the bytes. */
template <bool Little> class RELOCO_POINTER byte_emitter {
public:
  explicit byte_emitter(const sink &out) noexcept : out_(out) {}

  void u8(std::uint8_t value) noexcept { emit(value, 1); }
  void u16(std::uint16_t value) noexcept { emit(value, 2); }
  void u32(std::uint32_t value) noexcept { emit(value, 4); }
  void u64(std::uint64_t value) noexcept { emit(value, 8); }

  /** @brief Writes @p value using @p bytes bytes (1, 2, 4 or 8). */
  void emit(std::uint64_t value, unsigned bytes) noexcept {
    microfmt::array<char, 8> buffer{};
    const unsigned width = bytes < 8U ? bytes : 8U;
    for (unsigned i = 0; i < width; ++i) {
      const unsigned shift = Little ? i * 8U : (width - 1U - i) * 8U;
      // NOLINTNEXTLINE(reloco-narrowing-flow): the byte is masked to 0..255 and stored bit-for-bit
      buffer[i] = static_cast<char>(static_cast<unsigned char>((value >> shift) & 0xFFU));
    }
    write(string_view(buffer.data(), width));
  }

  void bytes(string_view data) noexcept { write(data); }

  /** @brief Writes @p count zero bytes. */
  void zeros(std::uint64_t count) noexcept {
    static constexpr microfmt::array<char, 64> zero_block{};
    while (count > 0) {
      const std::size_t n = count < zero_block.size() ? static_cast<std::size_t>(count) : zero_block.size();
      write(string_view(zero_block.data(), n));
      count -= n;
    }
  }

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

private:
  void write(string_view data) noexcept {
    out_.write(data);
    count_ += data.size();
  }

  sink out_;
  std::uint64_t count_{0};
};

/** @brief Size of a note header plus its name, for a name of @p name_length characters. */
constexpr std::uint64_t note_overhead(std::size_t name_length) noexcept {
  return 12U + ((name_length + 1U + 3U) & ~std::size_t{3});
}

/** @brief Writes a note header: sizes, type and the NUL-terminated name padded to 4 bytes. */
template <typename Emitter>
void write_note_header(Emitter &out, string_view name, std::uint64_t desc_size, std::uint32_t type) noexcept {
  const std::size_t name_size = name.size() + 1U;
  out.u32(static_cast<std::uint32_t>(name_size & 0xFFFFU));
  out.u32(static_cast<std::uint32_t>(desc_size & 0xFFFFFFFFU));
  out.u32(type);
  out.bytes(name);
  out.zeros(((name_size + 3U) & ~std::size_t{3}) - name.size());
}

/** @brief Marks a register slot that has no DWARF register number (stored as zero). */
inline constexpr std::uint32_t no_dwarf_register = 0xFFFFFFFFU;

/**
 * @brief Writes the part of a Linux `elf_prstatus` before the registers.
 *
 * It is the same for every architecture except for the width of the signal sets
 * and CPU-time fields: @p Is64 selects 8-byte or 4-byte words.
 */
template <bool Is64, typename Emitter> void write_prstatus_prefix(Emitter &out, const core_thread &thread) noexcept {
  // NOLINTNEXTLINE(reloco-narrowing-flow): signal numbers are non-negative; the bit pattern is what the note stores
  const auto signal = static_cast<std::uint32_t>(thread.signal);
  out.u32(signal); // si_signo
  out.u32(0);      // si_code
  out.u32(0);      // si_errno
  out.u16(static_cast<std::uint16_t>(signal & 0xFFFFU));
  out.u16(0);
  out.emit(0, Is64 ? 8 : 4); // pending signals
  out.emit(0, Is64 ? 8 : 4); // held signals
  out.u32(thread.id);
  out.u32(0);                  // parent id
  out.u32(0);                  // process group
  out.u32(0);                  // session
  out.zeros(Is64 ? 64U : 32U); // user/system CPU times
}

/** @brief Writes @p count register words, reading slot i through DWARF number `map(i)`; unreadable slots are zero. */
template <bool Is64, typename Emitter, typename Map>
void write_register_slots(Emitter &out, const core_thread &thread, std::uint32_t count, Map map) noexcept {
  using word_type = std::conditional_t<Is64, std::uint64_t, std::uint32_t>;
  for (std::uint32_t slot = 0; slot < count; ++slot) {
    const std::uint32_t dwarf = map(slot);
    word_type value = 0;
    if (dwarf == no_dwarf_register || !thread.regs.read(dwarf, value))
      value = 0;
    out.emit(value, sizeof(word_type));
  }
}

} // namespace detail

} // namespace microfmt::coredump
