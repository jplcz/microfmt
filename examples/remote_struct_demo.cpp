// SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
//
// SPDX-License-Identifier: MIT

#include <microfmt/inspector/remote_object.hpp>
#include <microfmt/microfmt.hpp>
#include <microfmt/sinks/stdio.hpp>

struct RemoteProcessInfo {
  uint32_t pid;
  microfmt::string32_ptr name;
  int32_t state;
  microfmt::compat32_ptr<uint32_t> user_flags;
};

// Define the remote structure layout
MICROFMT_REMOTE_STRUCT_BEGIN(RemoteProcessInfo)
MICROFMT_REMOTE_FIELD(uint32_t, pid)
MICROFMT_REMOTE_FIELD(microfmt::string32_ptr, name)
MICROFMT_REMOTE_FIELD(int32_t, state)
MICROFMT_REMOTE_FIELD(microfmt::compat32_ptr<uint32_t>, user_flags)
MICROFMT_REMOTE_STRUCT_END()

// Define a custom stateful address space context representing "foreign
// memory"
struct SimulatedMemorySpace {
  const void *struct_ptr{nullptr};
  uintptr_t string_addr{0};
  const char *string_data{nullptr};

  struct tag {};
};

// Provide address space traits specialization
namespace microfmt {
template <> struct address_space_traits<SimulatedMemorySpace::tag> {
  using context_type = SimulatedMemorySpace;

  static bool read_bytes(const void *ctx_ptr, uintptr_t addr, void *dest,
                         size_t size) noexcept {
    auto *ctx = static_cast<const SimulatedMemorySpace *>(ctx_ptr);
    // If reading the struct address
    if (addr == 0x10002000) {
      std::memcpy(dest, ctx->struct_ptr, size);
      return true;
    }
    return false;
  }

  static bool read_string(const void *ctx_ptr, uintptr_t addr, char *dest,
                          size_t max_len, size_t &out_len,
                          bool &null_term) noexcept {
    auto *ctx = static_cast<const SimulatedMemorySpace *>(ctx_ptr);
    if (addr == ctx->string_addr && ctx->string_data) {
      size_t len = std::strlen(ctx->string_data);
      size_t copy_n = (len < max_len) ? len : max_len;
      std::memcpy(dest, ctx->string_data, copy_n);
      if (copy_n < max_len) {
        dest[copy_n] = '\0';
        out_len = copy_n;
        null_term = true;
      } else {
        out_len = max_len;
        null_term = false;
      }
      return true;
    }
    return false;
  }
};
} // namespace microfmt

int main() {
  // Populate mock remote data layout (simulating 32-bit layout on a host)
  struct RawRemoteStruct {
    uint32_t pid;
    uint32_t name_ptr32;
    int32_t state;
    uint32_t flags_ptr32;
  } remote_instance{42,
                    0x50001000, // 32-bit pointer to string
                    1,          // Running
                    0x50002000};

  SimulatedMemorySpace memory_sim{&remote_instance, 0x50001000,
                                  "database_daemon"};

  // Create a type-erased address space reference bound to our simulated memory
  auto space =
      microfmt::address_space_ref::make<SimulatedMemorySpace::tag>(memory_sim);

  // Provide a scratch/work buffer (must be large enough to hold
  // sizeof(StructName) + chunk work buffer)
  std::byte scratch_buffer[256];

  // Instantiate the non-templated remote object view using the type tag
  microfmt::remote_object_view remote_obj(
      0x10002000, // Remote address of the struct
      space, microfmt::type_tag<RemoteProcessInfo>{}, scratch_buffer);

  // Format and print directly using microfmt!
  microfmt::println(MICROFMT_STRING("Process Details: {}"), remote_obj);

  // Output: Process Details: { pid: 42, name: database_daemon, state: 1,
  // user_flags: 0x50002000 }

  return 0;
}
