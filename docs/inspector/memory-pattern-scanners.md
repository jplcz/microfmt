<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Memory pattern scanners

The pattern scanner APIs search a bounded target-memory range without
allocating or copying the whole range. They read through an
`address_space_ref`, reuse caller-owned scratch storage, and return
`expected<memory_scan_result, address_space_error>`.

Use:

* `memory_pattern_scanner` for exact or masked byte signatures;
* `value_range_scanner<T>` for scalar values within inclusive bounds; and
* `dependent_value_scanner` for predicates spanning several related fields.

All scanners stop at the first match.

## Exact and masked byte patterns

`memory_pattern_scanner.hpp` provides the standard
`linear_memory_scanner_tag`. Its context contains an address space and the
scratch buffer used for chunked reads:

```cpp
std::byte scratch[64]{};
microfmt::memory_pattern_scanner<microfmt::linear_memory_scanner_tag> scanner{
    microfmt::linear_memory_scanner_context{space, scratch}};

const std::byte signature[]{
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef}};

auto result = scanner.ref().scan(start, length, signature);
if (!result) {
  // Inspect result.error().
} else if (result->found) {
  use_match(result->address);
}
```

The scratch buffer must be at least as large as the pattern. Reads overlap by
`pattern.size() - 1` bytes, so matches crossing a chunk boundary are found.
An empty pattern matches at `start`.

A mask has the same length as the pattern. Set mask bits to one where the
corresponding pattern bits must match and zero where they are ignored:

```cpp
const std::byte pattern[]{
    std::byte{0xde}, std::byte{0xad}, std::byte{0x00}, std::byte{0xef}};
const std::byte mask[]{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xff}};

auto result = scanner.ref().scan(start, length, pattern, mask);
```

This example accepts any third byte. Masks with a different size from the
pattern are rejected with `address_space_error::invalid_buffer`.

See `examples/memory_pattern_scanner_demo.cpp`.

## Scalar range scanning

`value_range_scanner<T>` interprets each candidate window as a trivially
copyable `T` and tests `minimum <= value && value <= maximum`:

```cpp
std::byte scratch[128]{};
microfmt::value_range_scanner<
    microfmt::linear_value_range_scanner_tag<uint32_t>, uint32_t>
    scanner{microfmt::linear_memory_scanner_context{space, scratch}};

auto result = scanner.ref().scan(start, length, 100U, 200U);
```

The default stride is `alignof(T)`. Supply another stride when the target ABI
uses packed fields or a known record width. A zero stride is normalized to one
byte. The scratch buffer must hold at least one `T`.

The scanner uses `memcpy` into a local `T`, so candidate addresses do not need
host alignment. The target byte order and representation must still match
`T`; for foreign ABIs, scan bytes or provide a custom trait that decodes the
target representation explicitly.

## Dependent window predicates

Use `dependent_value_scanner` when a candidate is valid only if several fields
satisfy an invariant. The callback receives the beginning of a window and an
optional caller context:

```cpp
struct requirements {
  uint32_t magic;
  uint16_t maximum_state;
};

bool matches(const void *bytes, void *opaque) noexcept {
  record candidate{};
  std::memcpy(&candidate, bytes, sizeof(candidate));
  const auto &required = *static_cast<const requirements *>(opaque);
  return candidate.magic == required.magic &&
         candidate.state <= required.maximum_state;
}

microfmt::dependent_value_scanner<
    microfmt::linear_dependent_value_scanner_tag>
    scanner{microfmt::linear_memory_scanner_context{space, scratch}};

requirements required{expected_magic, 7};
auto result = scanner.ref().scan(
    start, length, sizeof(record), sizeof(record), &matches, &required);
```

The predicate must be `noexcept`, must not retain the supplied window pointer,
and must read no more than the declared window size. The scanner invokes it
synchronously before reusing the scratch buffer.

See `examples/advanced_scanners_demo.cpp`.

## Custom scanner providers

The scanner types follow the inspector
[traits and contexts](traits-and-contexts.md) pattern. Specialize the
corresponding trait with a `context_type` and static `scan` operation, then
store that context in the typed scanner:

```cpp
struct mapped_pattern_scanner_tag {};

template <>
struct microfmt::memory_pattern_scanner_traits<mapped_pattern_scanner_tag> {
  using context_type = mapped_scanner_context;

  static microfmt::expected<microfmt::memory_scan_result,
                            microfmt::address_space_error>
  scan(microfmt::value_ref<const context_type> context,
       uintptr_t start, size_t length,
       microfmt::span<const std::byte> pattern,
       microfmt::span<const std::byte> mask) noexcept;
};
```

The typed owner retains `context_type` by value. `ref()` borrows it and may be
passed through the type-erased scanner API. The owner, its address-space
context, and its scratch storage must all outlive the scan.

## Error handling and safety

Scanner errors identify transport or configuration failures:

| Error | Meaning |
|---|---|
| `invalid_handle` | Scanner or address space is not bound |
| `invalid_address` | The address-space backend rejected the address |
| `invalid_buffer` | Scratch, mask, window, or predicate configuration is invalid |
| `read_failed` | The target transport could not read a requested chunk |

A successful result with `found == false` means the complete requested range
was searched without a match. This differs from an error, which means the
search could not be completed.

Treat target addresses and lengths as untrusted. Bound every search, use a
transport that validates target ranges, and choose scratch storage according
to the largest pattern or predicate window. `local_space_tag` is intended for
tests and self-inspection; it does not validate arbitrary addresses.
