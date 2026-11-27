// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

/** @file elf_core_writer.hpp
 * @brief Streams an ELF core file to a sink without allocating memory.
 *
 * Why: when a device crashes there is often no heap to rely on and no file
 * system to seek in. This writer produces the finished core file strictly front
 * to back, so the bytes can go to a serial port, a flash page writer, a socket
 * or a file through any `microfmt::sink`. Nothing is buffered except a
 * caller-provided scratch buffer.
 *
 * How: the file is produced by reading the same source twice.
 *  1. `plan()` walks the memory regions, finds which pages are readable, and
 *     counts threads and mapped files. The result (`core_plan`) is a handful of
 *     integers and gives the exact size of the file.
 *  2. `write()` walks the source again and writes the ELF header, program
 *     headers, notes and memory contents in file order. Offsets are known
 *     because pass 1 computed all sizes.
 *
 * Rules the caller must follow:
 *  - The target must be halted (no thread running, no DMA changing memory)
 *    between `plan()` and the end of `write()`. If it is not, the writer
 *    keeps the layout valid by padding with zeros and reports `plan_mismatch`.
 *  - The source must produce the same items in the same order in both passes.
 *
 * A source is any type providing templated visitors (no virtual calls):
 * @code
 *   template <class F> void for_each_region(F &&f) const;        // f(core_region)
 *   template <class F> void for_each_thread(F &&f) const;        // f(const core_thread &)
 *   template <class F> void for_each_mapped_file(F &&f) const;   // optional; f(const core_mapped_file &)
 *   template <class F> void for_each_auxv(F &&f) const;          // optional; f(std::uint64_t type, std::uint64_t
 * value)
 *   template <class F> void for_each_process_info(F &&f) const;  // optional; f(const core_process_info &)
 *   template <class F> void for_each_siginfo(F &&f) const;       // optional; f(const core_siginfo &)
 * @endcode
 * `span_core_source` adapts static tables.
 */

#include "../inspector/address_space.hpp"
#include "elf_format.hpp"
#include <type_traits>
#include <utility>

namespace microfmt::coredump {

/** @brief A source over caller-owned arrays; `files` and `auxv` may be empty. */
class RELOCO_POINTER span_core_source {
public:
  span_core_source(span<const core_region> regions RELOCO_LIFETIMEBOUND,
                   span<const core_thread> threads RELOCO_LIFETIMEBOUND,
                   span<const core_mapped_file> files RELOCO_LIFETIMEBOUND = {},
                   span<const core_auxv_entry> auxv RELOCO_LIFETIMEBOUND = {},
                   span<const core_process_info> process_info RELOCO_LIFETIMEBOUND = {},
                   span<const core_siginfo> siginfo RELOCO_LIFETIMEBOUND = {}) noexcept
      : regions_(regions), threads_(threads), files_(files), auxv_(auxv), process_info_(process_info),
        siginfo_(siginfo) {}

  template <typename F> void for_each_process_info(F &&visitor) const noexcept {
    for (const core_process_info &info : process_info_)
      visitor(info);
  }
  template <typename F> void for_each_siginfo(F &&visitor) const noexcept {
    for (const core_siginfo &info : siginfo_)
      visitor(info);
  }

  template <typename F> void for_each_region(F &&visitor) const noexcept {
    for (const core_region &region : regions_)
      visitor(region);
  }
  template <typename F> void for_each_thread(F &&visitor) const noexcept {
    for (const core_thread &thread : threads_)
      visitor(thread);
  }
  template <typename F> void for_each_mapped_file(F &&visitor) const noexcept {
    for (const core_mapped_file &file : files_)
      visitor(file);
  }
  template <typename F> void for_each_auxv(F &&visitor) const noexcept {
    for (const core_auxv_entry &entry : auxv_)
      visitor(entry.type, entry.value);
  }

private:
  span<const core_region> regions_;
  span<const core_thread> threads_;
  span<const core_mapped_file> files_;
  span<const core_auxv_entry> auxv_;
  span<const core_process_info> process_info_;
  span<const core_siginfo> siginfo_;
};

namespace detail {

template <typename Source, typename F, typename = void> struct has_mapped_files : std::false_type {};
template <typename Source, typename F>
struct has_mapped_files<Source, F,
                        std::void_t<decltype(std::declval<const Source &>().for_each_mapped_file(std::declval<F>()))>>
    : std::true_type {};

/** @brief Stand-in visitor used only to test whether a source offers `for_each_auxv`. */
struct auxv_probe {
  void operator()(std::uint64_t, std::uint64_t) const noexcept {}
};
template <typename Source, typename = void> struct has_auxv : std::false_type {};
template <typename Source>
struct has_auxv<Source, std::void_t<decltype(std::declval<const Source &>().for_each_auxv(auxv_probe{}))>>
    : std::true_type {};

constexpr std::uint64_t align4(std::uint64_t value) noexcept { return (value + 3U) & ~std::uint64_t{3}; }

/** @brief Stand-in visitors used only to test whether a source offers the optional process hooks. */
struct process_info_probe {
  void operator()(const core_process_info &) const noexcept {}
};
struct siginfo_probe {
  void operator()(const core_siginfo &) const noexcept {}
};
template <typename Source, typename = void> struct has_process_info : std::false_type {};
template <typename Source>
struct has_process_info<
    Source, std::void_t<decltype(std::declval<const Source &>().for_each_process_info(process_info_probe{}))>>
    : std::true_type {};
template <typename Source, typename = void> struct has_siginfo : std::false_type {};
template <typename Source>
struct has_siginfo<Source, std::void_t<decltype(std::declval<const Source &>().for_each_siginfo(siginfo_probe{}))>>
    : std::true_type {};

} // namespace detail

/**
 * @brief Writes an ELF core for architecture policy @p Arch from @p Source.
 *
 * `Arch` provides: `is_64bit`, `little_endian`, `machine`, `flags`, `page_size`,
 * `prstatus_size`, `write_prstatus(emitter, thread)`, `regset_count` and `regset(index)` (the per-thread
 * register sets to fetch as blobs, see `core_regset`; count 0 if none);
 * see `arch_x86_64` and `arch_arm_cortex_m`.
 */
template <typename Arch, typename Source> class RELOCO_POINTER elf_core_writer {
public:
  /**
   * @param source   items to dump; must outlive the writer
   * @param space    where memory contents are read from
   * @param scratch  chunk buffer for memory reads (at least 16 bytes, and at least as large as the largest
   *                 register set of the architecture, since each set is fetched in one read)
   * @param max_bytes budget for memory contents; headers and notes are always written
   * @param skip_zero_pages leave out pages that are entirely zero to shrink the dump; a debugger then reports
   *                        that memory as unavailable instead of showing zeros. Costs an extra read of all
   *                        memory in each pass.
   */
  elf_core_writer(const Source &source RELOCO_LIFETIMEBOUND, address_space_ref space,
                  span<char> scratch RELOCO_LIFETIMEBOUND, std::uint64_t max_bytes = ~std::uint64_t{0},
                  bool skip_zero_pages = false) noexcept
      : source_(source), space_(space), scratch_(scratch), max_bytes_(max_bytes), skip_zero_pages_(skip_zero_pages) {}

  elf_core_writer(Source &&, address_space_ref, span<char>, std::uint64_t = 0, bool = false) = delete;

  /** @brief Pass 1: sizes everything. The plan is invalid if the scratch buffer is too small. */
  [[nodiscard]] core_plan plan() const noexcept {
    core_plan result;
    if (scratch_.size() < min_scratch_size())
      return result;
    walk_segments([&result](std::uint64_t, std::uint64_t size, std::uint32_t) {
      ++result.load_segments;
      result.segment_bytes += size;
    });
    source_->for_each_thread([&result](const core_thread &) { ++result.threads; });
    for_each_file([&result](const core_mapped_file &file) {
      ++result.files;
      result.path_bytes += file.path.size() + 1U;
    });
    for_each_auxv([&result](std::uint64_t, std::uint64_t) { ++result.auxv_entries; });
    for_each_process_info([&result](const core_process_info &) { result.process_info = true; });
    for_each_siginfo([&result](const core_siginfo &) { result.siginfo = true; });
    result.note_bytes = result.threads * (note_overhead + detail::align4(Arch::prstatus_size) + regset_notes_size());
    if (result.process_info)
      result.note_bytes += note_overhead + detail::align4(detail::prpsinfo_size<Arch::is_64bit>);
    if (result.siginfo)
      result.note_bytes += note_overhead + detail::siginfo_size;
    if (result.files != 0)
      result.note_bytes += note_overhead + detail::align4(file_note_size(result.files, result.path_bytes));
    if (result.auxv_entries != 0)
      result.note_bytes += note_overhead + auxv_note_size(result.auxv_entries);
    result.total_size =
        elf_header_size + program_header_count(result) * program_header_size + result.note_bytes + result.segment_bytes;
    result.valid = true;
    return result;
  }

  /** @brief Pass 2: streams the file described by @p plan to @p out. */
  core_stats write(const sink &out, const core_plan &plan) const noexcept {
    core_stats stats;
    if (!plan.valid)
      return stats;
    detail::byte_emitter<Arch::little_endian> emitter(out);

    write_elf_header(emitter, plan);
    stats.plan_mismatch |= !write_program_headers(emitter, plan);
    stats.plan_mismatch |= !write_notes(emitter, plan);
    write_segments(emitter, plan, stats);

    if (emitter.count() < plan.total_size) {
      emitter.zeros(plan.total_size - emitter.count());
      stats.plan_mismatch = true;
    } else if (emitter.count() > plan.total_size) {
      stats.plan_mismatch = true;
    }
    stats.bytes_written = emitter.count();
    return stats;
  }

private:
  using emitter_type = detail::byte_emitter<Arch::little_endian>;

  static constexpr std::size_t min_scratch = 16;

  // Bytes of all register-set notes (headers included) of one thread.
  static constexpr std::uint64_t regset_notes_size() noexcept {
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < Arch::regset_count; ++i) {
      const core_regset set = Arch::regset(i);
      total += detail::note_overhead(set.name.size()) + detail::align4(set.size);
    }
    return total;
  }

  // Each register set is fetched with one read, so the scratch buffer must hold the largest one.
  static constexpr std::size_t min_scratch_size() noexcept {
    std::size_t largest = min_scratch;
    for (std::size_t i = 0; i < Arch::regset_count; ++i) {
      if (Arch::regset(i).size > largest)
        largest = Arch::regset(i).size;
    }
    return largest;
  }
  static constexpr std::uint64_t word_size = Arch::is_64bit ? 8 : 4;
  static constexpr std::uint64_t elf_header_size = Arch::is_64bit ? 64 : 52;
  static constexpr std::uint64_t program_header_size = Arch::is_64bit ? 56 : 32;
  static constexpr std::uint64_t note_overhead = 12 + 8;    // note header + "CORE\0" padded to 4 bytes
  static constexpr std::uint32_t max_load_segments = 65532; // keeps the program header count below PN_XNUM

  static constexpr std::uint64_t file_note_size(std::uint64_t files, std::uint64_t path_bytes) noexcept {
    return word_size * (2U + 3U * files) + path_bytes;
  }

  // Each entry is two words; the list ends with an extra (AT_NULL, 0) entry.
  static constexpr std::uint64_t auxv_note_size(std::uint64_t entries) noexcept {
    return word_size * 2U * (entries + 1U);
  }

  template <typename F> void for_each_auxv(F &&visitor) const noexcept {
    if constexpr (detail::has_auxv<Source>::value)
      source_->for_each_auxv(visitor);
  }
  template <typename F> void for_each_process_info(F &&visitor) const noexcept {
    if constexpr (detail::has_process_info<Source>::value)
      source_->for_each_process_info(visitor);
  }
  template <typename F> void for_each_siginfo(F &&visitor) const noexcept {
    if constexpr (detail::has_siginfo<Source>::value)
      source_->for_each_siginfo(visitor);
  }

  [[nodiscard]] static std::uint64_t program_header_count(const core_plan &plan) noexcept {
    return plan.load_segments + (plan.note_bytes != 0 ? 1U : 0U);
  }

  template <typename F> void for_each_file(F &&visitor) const noexcept {
    if constexpr (detail::has_mapped_files<Source, F>::value)
      source_->for_each_mapped_file(visitor);
  }

  [[nodiscard]] bool usable(std::uint64_t address, std::uint64_t next) const noexcept {
    if (!skip_zero_pages_) {
      char probe = 0;
      return space_.read_bytes(static_cast<uintptr_t>(address), &probe, 1).has_value();
    }
    // Zero-page skipping must look at the contents: a granule is kept only if it is readable and not all zero.
    while (address < next) {
      const std::uint64_t left = next - address;
      const std::size_t chunk = left < scratch_.size() ? static_cast<std::size_t>(left) : scratch_.size();
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
      char *const buffer = scratch_.data();
      RELOCO_END_UNSAFE_BUFFER_USAGE
      if (!space_.read_bytes(static_cast<uintptr_t>(address), buffer, chunk).has_value())
        return false;
      for (std::size_t i = 0; i < chunk; ++i) {
        if (scratch_[i] != 0)
          return true;
      }
      address += chunk;
    }
    return false;
  }

  // Splits regions into readable runs at page granularity; shared by both passes so they agree.
  template <typename F> void walk_segments(F &&visit) const noexcept {
    std::uint64_t budget = max_bytes_;
    std::uint32_t count = 0;
    source_->for_each_region([this, &budget, &count, &visit](const core_region &region) {
      const std::uint64_t end = region.address + region.size;
      if (end < region.address)
        return;
      std::uint64_t address = region.address;
      std::uint64_t run_start = 0;
      bool open = false;
      const auto flush = [&open, &run_start, &budget, &count, &visit, &region](std::uint64_t run_end) {
        if (!open)
          return;
        open = false;
        std::uint64_t length = run_end - run_start;
        if (length > budget)
          length = budget;
        if (length == 0 || count >= max_load_segments)
          return;
        visit(run_start, length, region.flags);
        budget -= length;
        ++count;
      };
      while (address < end) {
        std::uint64_t next = (address / Arch::page_size + 1U) * Arch::page_size;
        if (next > end || next < address)
          next = end;
        if (usable(address, next)) {
          if (!open) {
            open = true;
            run_start = address;
          }
        } else {
          flush(address);
        }
        address = next;
      }
      flush(end);
    });
  }

  static void word(emitter_type &out, std::uint64_t value) noexcept { out.emit(value, Arch::is_64bit ? 8 : 4); }

  static void write_elf_header(emitter_type &out, const core_plan &plan) noexcept {
    out.u8(0x7f);
    out.u8('E');
    out.u8('L');
    out.u8('F');
    out.u8(Arch::is_64bit ? 2 : 1);
    out.u8(Arch::little_endian ? 1 : 2);
    out.u8(1); // ELF version
    out.zeros(9);
    out.u16(et_core);
    out.u16(Arch::machine);
    out.u32(1);
    word(out, 0); // entry
    word(out, program_header_count(plan) != 0 ? elf_header_size : 0);
    word(out, 0); // no section headers
    out.u32(Arch::flags);
    out.u16(static_cast<std::uint16_t>(elf_header_size));
    out.u16(static_cast<std::uint16_t>(program_header_size));
    out.u16(static_cast<std::uint16_t>(program_header_count(plan)));
    out.u16(0);
    out.u16(0);
    out.u16(0);
  }

  static void write_program_header(emitter_type &out, std::uint32_t type, std::uint32_t flags, std::uint64_t offset,
                                   std::uint64_t address, std::uint64_t size, std::uint64_t align) noexcept {
    out.u32(type);
    if constexpr (Arch::is_64bit)
      out.u32(flags);
    word(out, offset);
    word(out, address);
    word(out, address);
    word(out, size);
    word(out, size);
    if constexpr (!Arch::is_64bit)
      out.u32(flags);
    word(out, align);
  }

  bool write_program_headers(emitter_type &out, const core_plan &plan) const noexcept {
    const std::uint64_t data_start = elf_header_size + program_header_count(plan) * program_header_size;
    std::uint64_t offset = data_start + plan.note_bytes;
    std::uint64_t written = 0;
    std::uint32_t segments = 0;
    if (plan.note_bytes != 0) {
      write_program_header(out, pt_note, 0, data_start, 0, plan.note_bytes, 1);
      ++written;
    }
    walk_segments(
        [&plan, &segments, &out, &offset, &written](std::uint64_t address, std::uint64_t size, std::uint32_t flags) {
          if (segments >= plan.load_segments)
            return;
          write_program_header(out, pt_load, flags, offset, address, size, 1);
          offset += size;
          ++segments;
          ++written;
        });
    const bool consistent = segments == plan.load_segments;
    for (; written < program_header_count(plan); ++written)
      out.zeros(program_header_size);
    return consistent;
  }

  static void note_header(emitter_type &out, std::uint64_t desc_size, std::uint32_t type) noexcept {
    detail::write_note_header(out, "CORE", desc_size, type);
  }

  bool write_notes(emitter_type &out, const core_plan &plan) const noexcept {
    const std::uint64_t start = out.count();
    std::uint32_t threads = 0;
    bool consistent = true;
    if (plan.process_info)
      consistent &= write_process_info_note(out);
    source_->for_each_thread([this, &plan, &threads, &out](const core_thread &thread) {
      if (threads >= plan.threads)
        return;
      note_header(out, Arch::prstatus_size, nt_prstatus);
      const std::uint64_t body = out.count();
      Arch::write_prstatus(out, thread);
      out.zeros(detail::align4(Arch::prstatus_size) - (out.count() - body));
      for (std::size_t i = 0; i < Arch::regset_count; ++i)
        write_regset(out, thread, Arch::regset(i));
      ++threads;
    });
    consistent &= threads == plan.threads;
    if (plan.siginfo)
      consistent &= write_siginfo_note(out);
    if (plan.files != 0)
      consistent &= write_file_note(out, plan);
    if (plan.auxv_entries != 0)
      consistent &= write_auxv_note(out, plan);
    const std::uint64_t used = out.count() - start;
    if (used < plan.note_bytes)
      out.zeros(plan.note_bytes - used);
    return consistent && used == plan.note_bytes;
  }

  // An unreadable set is written as zeros: its size is fixed by the plan, so the note cannot be left out.
  void write_regset(emitter_type &out, const core_thread &thread, const core_regset &set) const noexcept {
    detail::write_note_header(out, set.name, set.size, set.type);
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
    char *const buffer = scratch_.data();
    RELOCO_END_UNSAFE_BUFFER_USAGE
    if (set.size <= scratch_.size() && thread.regs.read_raw(regset_index(set.type), buffer, set.size))
      out.bytes(string_view(buffer, set.size));
    else
      out.zeros(set.size);
    out.zeros(detail::align4(set.size) - set.size);
  }

  // Both notes describe the whole process, so only the first item the source offers is written.
  bool write_process_info_note(emitter_type &out) const noexcept {
    bool written = false;
    for_each_process_info([&out, &written](const core_process_info &info) {
      if (written)
        return;
      note_header(out, detail::prpsinfo_size<Arch::is_64bit>, nt_prpsinfo);
      detail::write_prpsinfo<Arch::is_64bit>(out, info);
      out.zeros(detail::align4(detail::prpsinfo_size<Arch::is_64bit>) - detail::prpsinfo_size<Arch::is_64bit>);
      written = true;
    });
    return written;
  }

  bool write_siginfo_note(emitter_type &out) const noexcept {
    bool written = false;
    for_each_siginfo([&out, &written](const core_siginfo &info) {
      if (written)
        return;
      note_header(out, detail::siginfo_size, nt_siginfo);
      detail::write_siginfo<Arch::is_64bit>(out, info);
      written = true;
    });
    return written;
  }

  bool write_auxv_note(emitter_type &out, const core_plan &plan) const noexcept {
    note_header(out, auxv_note_size(plan.auxv_entries), nt_auxv);
    std::uint32_t entries = 0;
    std::uint32_t seen = 0;
    for_each_auxv([&plan, &entries, &seen, &out](std::uint64_t type, std::uint64_t value) {
      ++seen;
      if (entries >= plan.auxv_entries)
        return;
      word(out, type);
      word(out, value);
      ++entries;
    });
    for (; entries < plan.auxv_entries; ++entries)
      out.zeros(word_size * 2U); // keeps the size when the source shrank
    word(out, 0);                // AT_NULL terminator
    word(out, 0);
    return seen == plan.auxv_entries;
  }

  bool write_file_note(emitter_type &out, const core_plan &plan) const noexcept {
    const std::uint64_t size = file_note_size(plan.files, plan.path_bytes);
    note_header(out, size, nt_file);
    word(out, plan.files);
    word(out, Arch::page_size);
    std::uint32_t entries = 0;
    for_each_file([&plan, &entries, &out](const core_mapped_file &file) {
      if (entries >= plan.files)
        return;
      word(out, file.start);
      word(out, file.end);
      word(out, file.offset / Arch::page_size);
      ++entries;
    });
    std::uint32_t paths = 0;
    for_each_file([&plan, &paths, &out](const core_mapped_file &file) {
      if (paths >= plan.files)
        return;
      out.bytes(file.path);
      out.u8(0);
      ++paths;
    });
    out.zeros(detail::align4(size) - size);
    return entries == plan.files && paths == plan.files;
  }

  void write_segments(emitter_type &out, const core_plan &plan, core_stats &stats) const noexcept {
    const std::uint64_t start = out.count();
    std::uint32_t segments = 0;
    walk_segments(
        [this, &plan, &segments, &out, &start, &stats](std::uint64_t address, std::uint64_t size, std::uint32_t) {
          if (segments >= plan.load_segments)
            return;
          ++segments;
          const std::uint64_t room = plan.segment_bytes - (out.count() - start);
          write_segment_data(out, address, size < room ? size : room, stats);
        });
    const std::uint64_t used = out.count() - start;
    if (used < plan.segment_bytes) {
      out.zeros(plan.segment_bytes - used);
      stats.plan_mismatch = true;
    }
  }

  void write_segment_data(emitter_type &out, std::uint64_t address, std::uint64_t size,
                          core_stats &stats) const noexcept {
    while (size != 0) {
      const std::size_t chunk = size < scratch_.size() ? static_cast<std::size_t>(size) : scratch_.size();
      RELOCO_BEGIN_UNSAFE_BUFFER_USAGE
      char *const buffer = scratch_.data();
      RELOCO_END_UNSAFE_BUFFER_USAGE
      if (space_.read_bytes(static_cast<uintptr_t>(address), buffer, chunk).has_value()) {
        out.bytes(string_view(buffer, chunk));
      } else {
        ++stats.read_failures;
        out.zeros(chunk);
      }
      address += chunk;
      size -= chunk;
    }
  }

  value_ref<const Source> source_;
  address_space_ref space_;
  span<char> scratch_;
  std::uint64_t max_bytes_;
  bool skip_zero_pages_;
};

} // namespace microfmt::coredump
