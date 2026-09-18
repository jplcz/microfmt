<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Inspector: architectures and registers

The inspector represents target CPU state through `register_context_ref` and
describes architecture register sets with constexpr catalogs. Unwinders,
register dumps, pointer discovery, and target-specific decoders can therefore
use the same register indexes without owning a concrete trap-frame or debugger
context type.

## Register contexts

`register_context_ref` is a borrowed, type-erased handle containing:

* an opaque target-state pointer;
* a compile-time-selected `register_context_traits<Tag>` dispatch table;
* the target `address_space_ref`; and
* caller-owned scratch storage.

Trait operations receive architecture register indexes from
`dwarf_registers.hpp`. The standard GPR, floating-point, and vector indexes
follow the architecture's DWARF convention where one exists. System,
privileged, debug, and generic-timer registers use microfmt's extended index
range and must be interpreted together with the selected architecture.

```cpp
struct register_file {
  uint64_t values[128];
};

bool read_register(const void *state, microfmt::address_space_ref,
                   uint32_t index, void *output, size_t size) noexcept;

bool write_register(void *state, microfmt::address_space_ref,
                    uint32_t index, const void *input, size_t size) noexcept;

using register_tag = microfmt::read_write_register_context_tag<
    register_file, read_register, write_register>;

register_file registers{};
std::byte scratch[16]{};
microfmt::register_context_ref context{
    register_tag{}, registers, target_space, scratch};
```

A context is valid when its state is bound and its traits provide at least one
operation. Read-only and write-only traits are supported. An operation should
return `false` for unavailable registers, unsupported widths, invalid indexes,
or failed target reads; register views skip unavailable entries.

`register_context<Tag>` can retain `traits::context_type` by value and expose
mutable or read-only refs. The callback-tag helpers are convenient adapters
for existing free functions while keeping their selection at compile time.

The state, address-space context, and scratch storage must outlive every
unwinder or view that borrows the handle.

### `ucontext_adapter.hpp`: a ready-made POSIX `ucontext_t` backend

`ucontext_adapter.hpp` provides ready-to-use `register_context_ref` factories
for Linux and FreeBSD (guarded by an `#error` on other OSes), backed by the
POSIX `ucontext_t`/`mcontext_t` structures delivered to a `SA_SIGINFO` signal
handler's third argument or captured by `getcontext()`. It covers x86, x86_64,
ARM, AArch64, and RISC-V, mapping each architecture's DWARF register indexes
onto the OS-specific `gregs`/`mcontext_t` layout:

```cpp
#include <microfmt/inspector/ucontext_adapter.hpp>

void handler(int, siginfo_t *, void *ucontext_raw) noexcept {
  auto &uctx = *static_cast<ucontext_t *>(ucontext_raw);
  const microfmt::address_space_ref space{microfmt::local_space_tag{}};
  std::byte scratch[16];

  auto context =
      microfmt::make_ucontext_register_context_ref(uctx, space, scratch);
  // context.read(microfmt::dwarf::x86_64::rip, ...), register_context_view, ...
}
```

`make_ucontext_register_context_ref` returns a read-only context; use
`make_mutable_ucontext_register_context_ref` (taking a non-const `ucontext_t&`)
when the caller also needs to rewrite registers in place, for example
redirecting `pc`/`sp` before a `setcontext()`/`swapcontext()` resume. Both
factories are zero-allocation and `noexcept`; unsupported register indexes or
architecture/OS combinations return `false` from reads/writes without
touching the output.

### Bind register structures at compile time

`register_context_ref_with<State, FieldTraits...>` adapts an existing C or C++
register structure without writing a manual traits specialization. For ordinary
members, use `register_member_field` with a pointer to member and one or more
register indexes:

```cpp
struct saved_registers {
  uint32_t pc;
  uint32_t sp;
};

using pc_field = microfmt::register_member_field<
    &saved_registers::pc, microfmt::dwarf::arm32::pc>;
using sp_field = microfmt::register_member_field<
    &saved_registers::sp, microfmt::dwarf::arm32::sp>;

saved_registers saved{};
std::byte scratch[16]{};
microfmt::register_context_ref_with<saved_registers, pc_field, sp_field>
    mapped{saved, target_space, scratch};
microfmt::register_context_ref context = mapped.ref();
```

Multiple indexes in one `register_member_field` alias the same member. Field
traits are checked in declaration order, and the first matching trait handles
the request.

For nested structures, arrays, banked registers, or mode-dependent selection,
use `register_selected_field` or define a custom field trait. A selected field
receives functions that inspect the complete source state before returning the
active member. For example, ARM code can select R7 or R11 as the frame pointer
from CPSR.T:

```cpp
struct arm_saved_registers {
  uint32_t cpsr;
  uint32_t r7;
  uint32_t r11;
};

struct select_fp {
  template <typename Registers>
  constexpr auto operator()(Registers &state, uint32_t) const noexcept
      -> decltype(&state.r7) {
    return (state.cpsr & (1U << 5)) != 0 ? &state.r7 : &state.r11;
  }
};

using fp_field = microfmt::register_selected_field<
    arm_saved_registers, uint32_t, select_fp,
    microfmt::dwarf::arm32::fp>;
```

The selector's templated call operator returns `uint32_t *` for mutable state
and `const uint32_t *` for const state. To make a selected field read-only,
return `const value_type *` for both invocations. A fully custom trait can
implement the same `state_type`, `value_type`, `matches(index)`, and
`get(state, index)` surface to select nested arrays such as
`other.child[index]`. Constructing the wrapper from a `const State` also
disables all mapped writes. Register reads and writes require the requested
byte width to exactly match the selected field type.

Registers that are queried rather than stored can use
`register_callback_field`. Its typed read callback receives the bound state,
address space, register index, and output value. The optional write callback
has the corresponding mutable signature; pass `nullptr` for registers that
are read-only:

```cpp
bool read_status(const device &state, microfmt::address_space_ref,
                 uint32_t, uint32_t &value) noexcept {
  value = state.read_status_register();
  return true;
}

using status_field = microfmt::register_callback_field<
    device, uint32_t, read_status, nullptr, status_register_index>;
```

This callback form can execute a platform register-access primitive directly
and can report that a value is temporarily unavailable by returning `false`.

The constructor accepts an optional fallback `register_context_ref`. Unmatched
registers are delegated to it, so wrappers can overlay a few fields on a
platform context or chain several mapped structures. A matched field that
rejects an operation does not fall through, preventing an invalid width or
read-only field from being silently handled by a later context.

The wrapper owns no register data. It and the bound state must outlive every
`register_context_ref` returned by `ref()`. Internally, the required
read-only state borrow is represented by `value_ref<const State>`, while a nullable
`value_ptr<State>` records whether mutation is permitted. The type-erased
`register_context_ref` follows the same pattern with separate const and
mutable state borrows, avoiding erased `const_cast` operations.

## Architecture register catalogs

`dwarf_registers.hpp` defines register namespaces for every supported ABI
family:

| Namespace | ABI traits |
|---|---|
| `dwarf::arm32` | `arm_abi_traits` |
| `dwarf::aarch64` | `aarch64_abi_traits` |
| `dwarf::x86` | `x86_abi_traits` |
| `dwarf::x86_64` | `x86_64_abi_traits` |
| `dwarf::riscv` | `riscv32_abi_traits`, `riscv64_abi_traits` |

Each ABI exposes its catalog as `AbiTraits::register_traits`. A catalog uses:

```cpp
struct register_descriptor {
  microfmt::string_view name;
  uint32_t index;
};
```

Register-number identifiers are lowercase, for example
`dwarf::x86_64::rax`, `dwarf::arm32::r0`, and
`dwarf::aarch64::tpidr_el0`. This avoids collisions with architecture headers
that define uppercase register-name macros. Descriptor names remain uppercase
for conventional debugger and diagnostic output.

The following constexpr surfaces are available:

* `register_traits::gpr_registers` contains the architecture's printable
  general-purpose register names and indexes.
* `register_traits::system_registers` contains available status, control,
  exception, MMU, debug, identification, CSR, and timer descriptors.
* `register_traits::address_registers()` returns an ordered constexpr array of
  GPR descriptors whose values are useful pointer-discovery candidates.

The address candidates are ordered heuristically: frame pointers first,
followed by ABI argument registers, callee-saved registers, and temporaries.
Stack pointers, link/return-address registers, program counters, and RISC-V
`zero` are deliberately excluded. This list is intended to prioritize scanning
or annotation; a candidate value still requires target-range and alignment
validation before it is treated as an address.

```cpp
using traits = microfmt::aarch64_abi_traits::register_traits;

for (const auto &reg : traits::address_registers()) {
  uint64_t value = 0;
  if (context.read(reg.index, value) && target_range.contains(value)) {
    // reg.name identifies the likely pointer-bearing register.
  }
}
```

Each built-in ABI also exposes `AbiTraits::gdb_register_traits`, which maps
GDB register numbers and names to the corresponding DWARF indexes and bit
widths. Its `layout()`, `extended_layout()`, and `non_standard_layout()`
surfaces keep core `g`/`G` registers separate from floating-point/vector and
target-specific system registers. Lookups by GDB number, DWARF index, or name
search all three layouts.

The GDB register-array codec and target XML printer use the core layout.
Callers can publish the other two layouts as separate architectural or
vendor-specific GDB features. One ABI trait type therefore selects the
register mappings for unwinding, register I/O, and GDB protocol output:

```cpp
using abi = microfmt::aarch64_abi_traits;

microfmt::gdb::register_array_encoder::encode_all_registers<abi>(
    output, context);
microfmt::gdb::register_xml_printer::format_target_xml<abi>(
    output, "aarch64");
```

## ARM and AArch64 system registers

ARM32 and AArch64 catalogs include extended descriptors for execution state,
exception handling, thread identifiers, MMU configuration, identification,
virtualization, and generic timers.

The architecture-neutral `dwarf::generic_timer` namespace assigns stable
extended indexes for the architectural timer families. ARM32 and AArch64
provide their conventional names as aliases, for example:

* `arm32::cntfrq`, `arm32::cntpct`, and `arm32::cntv_ctl`;
* `aarch64::cntfrq_el0`, `aarch64::cntpct_el0`, and
  `aarch64::cntv_ctl_el0`;
* hypervisor timer controls such as `cnthctl`, `cnthp_cval`, and their
  AArch64 `_EL2` aliases.

These indexes identify logical registers for inspector callbacks; they are not
raw instruction encodings such as AArch64 `op0/op1/CRn/CRm/op2`.

## Register rendering

Wrap a context in `register_context_view<AbiTraits>` to print the GPR and
system descriptors supplied by the architecture catalog:

```cpp
auto view =
    microfmt::register_context_view<microfmt::aarch64_abi_traits>{context};
microfmt::format_to(output, MICROFMT_STRING("{}"), view);
```

Only successfully read registers are emitted. Output is grouped by target
register width:

* 32-bit ABIs render three registers per line;
* 64-bit ABIs render two registers per line.

AArch64 LR and PC values are normalized through
`aarch64_abi_traits::normalize_pc` before display. Other consumers that need
raw PAC, tag, or mode bits should read the context directly.

## Adding an architecture or register

When extending the catalog:

1. Preserve standard DWARF register numbers and existing extended IDs.
2. Add the lowercase constant to the architecture namespace; keep the
   descriptor's display name in the architecture's conventional case.
3. Add a `register_descriptor` to `gpr_registers` or `system_registers`.
4. Add pointer-bearing GPRs to `address_registers()` in probability order,
   without SP, LR/RA, PC, or fixed-zero registers.
5. Expose the catalog as `AbiTraits::register_traits` and its GDB mapping as
   `AbiTraits::gdb_register_traits`.
6. Make the register-context callback accept the requested value width and
   return `false` when the target does not provide that register.

Do not add aliases as duplicate descriptors in one catalog: descriptor indexes
must remain unique within each returned array.

## Primary use case: an in-process crash handler

`examples/crash_handler_demo.cpp` is a complete, standalone program combining
this guide's `ucontext_adapter.hpp` backend and `register_context_view` with
`dl_symbol_resolver.hpp` from
[Symbols and diagnostics](symbols-and-diagnostics.md). It installs a real
`SIGSEGV`/`SIGBUS` handler that, on a genuine fault, reads the delivered
`ucontext_t` into a `register_context_ref`, renders every register, and
resolves the faulting program counter to a demangled `image!symbol+offset`
string — writing only through `fd_sink`'s raw `write(2)` to stay
async-signal-safe. It is a good end-to-end reference for wiring register
contexts into a real signal handler.

