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
* read and optional write callbacks;
* the target `address_space_ref`; and
* caller-owned scratch storage.

Callbacks receive architecture register indexes from
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

register_file registers{};
std::byte scratch[16]{};
microfmt::register_context_ref context{
    &registers, {read_register, write_register}, target_space, scratch};
```

A context is valid when its state is non-null and at least one callback is
available. Read-only and write-only contexts are supported. A callback should
return `false` for unavailable registers, unsupported widths, invalid indexes,
or failed target reads; register views skip unavailable entries.

The state, address-space context, and scratch storage must outlive every
unwinder or view that borrows the handle.

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
widths. The GDB register-array codec and target XML printer consume this
binding directly, so callers select one ABI trait type for unwinding, register
I/O, and GDB protocol output:

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

* `arm32::CNTFRQ`, `arm32::CNTPCT`, and `arm32::CNTV_CTL`;
* `aarch64::CNTFRQ_EL0`, `aarch64::CNTPCT_EL0`, and
  `aarch64::CNTV_CTL_EL0`;
* hypervisor timer controls such as `CNTHCTL`, `CNTHP_CVAL`, and their
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
2. Add the constant to the architecture namespace.
3. Add a `register_descriptor` to `gpr_registers` or `system_registers`.
4. Add pointer-bearing GPRs to `address_registers()` in probability order,
   without SP, LR/RA, PC, or fixed-zero registers.
5. Expose the catalog as `AbiTraits::register_traits` and its GDB mapping as
   `AbiTraits::gdb_register_traits`.
6. Make the register-context callback accept the requested value width and
   return `false` when the target does not provide that register.

Do not add aliases as duplicate descriptors in one catalog: descriptor indexes
must remain unique within each returned array.
