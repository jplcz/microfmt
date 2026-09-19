// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#include <cstdint>
#include <cstring>
#include <microfmt/inspector/address_space.hpp>
#include <microfmt/inspector/compat32.hpp>
#include <microfmt/sinks/stdio.hpp>

// ============================================================================
// User-Defined Foreign Address Space (e.g., Guest VM / Tracer Sandbox)
// ============================================================================

struct simulated_guest_space_tag {};

struct simulated_guest_context {
  uint32_t vm_id;
  uintptr_t host_base_addr;
  size_t memory_limit;
};

template <> struct microfmt::address_space_traits<simulated_guest_space_tag> {
  using context_type = simulated_guest_context;

  static bool read_bytes(microfmt::value_ref<const context_type> context,
                         uintptr_t addr, void *dest, size_t size) noexcept {
    if (addr == 0 || !dest)
      return false;

    // Boundary check within simulated memory window
    if (addr + size > context->memory_limit || addr + size < addr) {
      return false;
    }

    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    const auto *src =
        reinterpret_cast<const void *>(context->host_base_addr + addr);
    std::memcpy(dest, src, size);

    RELOCO_END_UNSAFE_BUFFER_USAGE;

    return true;
  }

  static bool read_string(microfmt::value_ref<const context_type> context,
                          uintptr_t addr, char *dest, size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

    if (addr == 0 || !dest || max_len == 0)
      return false;

    if (addr >= context->memory_limit)
      return false;

    const auto *src =
        reinterpret_cast<const char *>(context->host_base_addr + addr);
    size_t available = context->memory_limit - addr;
    size_t limit = (max_len < available) ? max_len : available;

    size_t i = 0;
    while (i < limit) {
      dest[i] = src[i];
      if (dest[i] == '\0') {
        out_len = i;
        null_term = true;
        return true;
      }
      ++i;
    }

    out_len = limit;
    null_term = false;
    return true;
    RELOCO_END_UNSAFE_BUFFER_USAGE;
  }
};

// ============================================================================
// Data Structures (Native & 32-bit Compact)
// ============================================================================

struct alignas(8) NativeHostTask {
  uint64_t task_id;
  uint32_t flags;
  const char *name;
};

template <> struct microfmt::formatter<NativeHostTask> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const NativeHostTask &task, const sink &out) const noexcept {
    microfmt::format_to(out, "NativeHostTask(id={}, flags={:#x}, name_ptr={})", task.task_id, task.flags,
                        reinterpret_cast<const void *>(task.name));
  }
};

struct alignas(4) Compat32GuestTask {
  uint32_t task_id;
  uint16_t priority;
  uint16_t state_flags;
  microfmt::compat32_ptr<const char> comm_name;
  microfmt::compat32_ptr<void> stack_base;
};

static_assert(sizeof(Compat32GuestTask) == 16, "Compat32GuestTask must be 16 bytes");
static_assert(alignof(Compat32GuestTask) == 4, "Compat32GuestTask must be 4-byte aligned");

template <> struct microfmt::formatter<Compat32GuestTask> {
  constexpr void parse(format_parse_context &) noexcept {}

  void format(const Compat32GuestTask &task, const sink &out) const noexcept {
    microfmt::format_to(out, "Compat32GuestTask(id={}, prio={}, flags={:#x}, comm={}, stack={})", task.task_id,
                        task.priority, task.state_flags, task.comm_name, task.stack_base);
  }
};

// ============================================================================
// Main Demonstration
// ============================================================================

int main() {
  microfmt::println("=========================================================="
                    "======================");
  microfmt::println("         microfmt Type-Erased Address Space & Compat32 Demo");
  microfmt::println("=========================================================="
                    "======================");

  // --------------------------------------------------------------------------
  // Scenario A: Stateless Local Host Address Space
  // --------------------------------------------------------------------------
  const char local_task_name[] = "kworker/u16:2";
  NativeHostTask local_task{10024, 0x0004'0001, local_task_name};

  microfmt::address_space_ref host_space(microfmt::local_space_tag{});

  alignas(alignof(NativeHostTask)) std::byte host_task_scratch[sizeof(NativeHostTask)];
  char host_str_scratch[32];

  microfmt::remote_ref<NativeHostTask> host_task_ref(reinterpret_cast<uintptr_t>(&local_task), host_space,
                                                     host_task_scratch);

  microfmt::remote_string_view host_name_ref(reinterpret_cast<uintptr_t>(local_task_name), host_space,
                                             host_str_scratch);

  microfmt::println("\n[1. Host Local Space (Stateless)]");
  microfmt::println("Task Object  : {}", host_task_ref);
  microfmt::println("Task Comm    : {}", host_name_ref);

  // --------------------------------------------------------------------------
  // Scenario B: Stateful Foreign Simulated Guest Space with 32-bit Types
  // --------------------------------------------------------------------------
  RELOCO_BEGIN_UNSAFE_BUFFER_USAGE;

  alignas(16) uint8_t simulated_ram[1024];
  std::memset(simulated_ram, 0, sizeof(simulated_ram));

  // Place guest string at guest relative address 0x0000'0080
  const char guest_comm[] = "init_service32";
  std::memcpy(&simulated_ram[0x80], guest_comm, sizeof(guest_comm));

  RELOCO_END_UNSAFE_BUFFER_USAGE;

  // Place 32-bit guest struct at guest relative address 0x0000'0100
  auto *guest_task_mem = static_cast<Compat32GuestTask *>(static_cast<void *>(&simulated_ram[0x100]));
  guest_task_mem->task_id = 1;
  guest_task_mem->priority = 100;
  guest_task_mem->state_flags = 0x0002;
  guest_task_mem->comm_name = microfmt::compat32_ptr<const char>(0x0000'0080);
  guest_task_mem->stack_base = microfmt::compat32_ptr<void>(0x0040'0000);

  simulated_guest_context guest_ctx{
      .vm_id = 42, .host_base_addr = reinterpret_cast<uintptr_t>(simulated_ram), .memory_limit = sizeof(simulated_ram)};
  microfmt::address_space_ref guest_space(simulated_guest_space_tag{}, guest_ctx);

  alignas(alignof(Compat32GuestTask)) std::byte guest_task_scratch[sizeof(Compat32GuestTask)];
  char guest_str_scratch[16]; // Small 16-byte chunk buffer

  microfmt::remote_ref<Compat32GuestTask> guest_task_ref(0x100, guest_space, guest_task_scratch);

  microfmt::println("\n[2. Simulated Guest Space (Stateful & Compat32)]");
  microfmt::println("Guest Struct : {}", guest_task_ref);

  // Dereference the 32-bit string pointer loaded from guest struct
  auto staged_guest_task = guest_task_ref.load();
  if (staged_guest_task) {
    auto remote_comm_view =
        microfmt::make_remote_string32((*staged_guest_task)->comm_name, guest_space, guest_str_scratch);
    microfmt::println("Resolved Comm: {}", remote_comm_view);
  }

  // --------------------------------------------------------------------------
  // Scenario C: Fault Handling & Safety Boundaries
  // --------------------------------------------------------------------------
  microfmt::println("\n[3. Fault & Boundary Protections]");

  // Null pointer inspect
  microfmt::remote_ref<Compat32GuestTask> null_task_ref(0, guest_space, guest_task_scratch);
  microfmt::remote_string_view null_str_ref(0, guest_space, guest_str_scratch);
  microfmt::println("Null Struct  : {}", null_task_ref);
  microfmt::println("Null String  : {}", null_str_ref);

  // Out of bounds address inspect (limit is 1024)
  microfmt::remote_ref<Compat32GuestTask> fault_task_ref(0x2000, guest_space, guest_task_scratch);
  microfmt::remote_string_view fault_str_ref(0x2000, guest_space, guest_str_scratch);
  microfmt::println("Fault Struct : {}", fault_task_ref);
  microfmt::println("Fault String : {}", fault_str_ref);

  return 0;
}