<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Ranges and structured values

These formatters traverse existing values without collecting their output in
an intermediate container. Their views borrow the underlying range,
descriptor, predicate, or reflected object.

For fixed storage in firmware and kernel-oriented code, prefer
`microfmt::array` and expose it as a `microfmt::span` when an API needs a
contiguous view. Standard ranges remain supported for interoperability and
application code where their allocation and access behavior is intentional.

## Shared delimiter convention

Several range-like formatters consume a leading delimiter flag and forward
the remaining specifier to their elements:

| Flag | Delimiters |
|---|---|
| `b` | `[` and `]` |
| `c` | `{` and `}` |
| `n` | No outer delimiters |
| `p` | `(` and `)`; tuple formatter only |

## `ranges.hpp`

`join(range, delimiter)` and `join(first, last, delimiter)` create a
`join_view`. The complete replacement-field specifier is forwarded to every
element.

```cpp
microfmt::format_to(out, "{:04X}", microfmt::join(values, ", "));
```

In C++20 and later, `join_as<Delimiter, ElementSpec>(range)` stores delimiter
and element formatting in the type. A runtime specifier is used only when the
compile-time `ElementSpec` is empty. See `examples/ranges_demo.cpp`.

## `filter_view.hpp`

`filter(range, predicate)` and `filter(pointer, count, predicate)` create a
view that visits only matching elements.

```cpp
auto even = microfmt::filter(values, [](int value) {
  return value % 2 == 0;
});
microfmt::format_to(out, "{:n}", even);
```

Use `b`, `c`, or `n` for outer delimiters. The remainder of the specifier is
forwarded to each selected element. See `examples/filter_view_demo.cpp`.

## `repeated_view.hpp`

`repeat(value, count)` and `repeat(value, count, separator)` create a view
that formats the same value `count` times, optionally joined by `separator`.

```cpp
microfmt::format_to(out, "{}", microfmt::repeat('*', 5));
microfmt::format_to(out, "{:04x}", microfmt::repeat(0x2A, 3, "|"));
```

The complete replacement-field specifier is forwarded to the wrapped value's
formatter and re-applied on every repetition.

## `map_view.hpp`

`map_view(container)` formats standard map-like entries as
`key: value`. Overloads accept custom key/value extractors and explicit
iterator/sentinel pairs for non-standard layouts.

```cpp
microfmt::format_to(out, "{}", microfmt::map_view(settings));
```

Use `b`, `c`, or `n` for delimiters. The remaining specifier is forwarded to
both keys and values. The iterator overload requires a dereferenceable
iterator. See `examples/map_view_demo.cpp`.

## `tuple.hpp`

Tuple-like types detected through `std::tuple_size`, including
`std::tuple` and `std::pair`, are formatted directly. Types exposing
`.data()` are excluded to avoid treating array-like containers as tuples.

Use `b`, `c`, `n`, or `p` for delimiters. The remaining specifier is forwarded
to every tuple element.

```cpp
microfmt::format_to(out, "{:p04X}", std::tuple{1, 2, 3});
```

See `examples/tuple_demo.cpp`.

## `reloco.hpp`

Formats concrete `jplcz_reloco` container/value types **directly** -- no
adapter call needed:

* `formatter<reloco::vector<T>>` and `formatter<reloco::inline_vector<T,
  Capacity>>` format as `[val1, val2, ...]`.
* `formatter<reloco::flat_set<T, Compare>>` and
  `formatter<reloco::inline_flat_set<T, Capacity, Compare>>` format their
  (sorted, unique) elements as `[val1, val2, ...]`.
* `formatter<reloco::flat_map<Key, Mapped, Compare>>` and
  `formatter<reloco::inline_flat_map<Key, Mapped, Capacity, Compare>>` format
  as `{key1: val1, key2: val2, ...}`.
* `formatter<reloco::basic_string<CharT, TraitsT>>`,
  `formatter<reloco::basic_inline_string<Capacity, CharT, TraitsT>>`, and
  `formatter<reloco::basic_sso_string<CharT, TraitsT>>` write the string's
  characters directly (delegating to `.view()`), the same as
  `microfmt::string_view`.
* `formatter<microfmt::value_ptr<T>>`, `formatter<reloco::unique_ptr<T>>`,
  and `formatter<reloco::shared_ptr<T>>` format the pointee's value directly,
  or the literal text `(null)` when empty.
* `formatter<microfmt::value_ref<T>>` always formats the referenced value
  directly (never null).
* `formatter<reloco::weak_ptr<T>>` attempts to `lock()` the pointee: formats
  its value if still alive, or the literal text `(expired)` otherwise.
* `formatter<reloco::duration>` formats as `sec.frac` with a trailing `s`
  unit, mirroring `formatters/posix_time.hpp`'s `timespec`/`timeval`
  formatters (which `reloco::duration` shares its `(seconds,
  subsec_nanoseconds)` representation with). Accepts the same `m`/`3`,
  `u`/`6`, `n`/`9` fractional-precision suffixes (default `9`, nanoseconds)
  and the `r`/`R` flag to suppress the trailing `s` unit.
* `formatter<reloco::instant>` formats as `HH:MM:SS.mmm`: the value's own
  elapsed time since the default-constructed ("zero") `instant`, the
  closest analog to `std::chrono::steady_clock`'s own opaque epoch --
  `instant` itself carries no defined epoch (see `<reloco/instant.hpp>`),
  so it cannot be rendered as a calendar timestamp.

The replacement-field specifier (e.g. `{:04X}`) cascades down to each
element/value (for maps, both keys and values receive it).

```cpp
#include <microfmt/formatters/reloco.hpp>

reloco::vector<int> values = ...;
microfmt::format_to(out, "{}", values);
```

`reloco::function_ref<Sig>` intentionally has **no** formatter: there is no
safe way to introspect or print a bound callable's identity without invoking
it (which would have side effects).

### Type-erased container adapters (fallback)

The formatters above cover concrete types directly. For containers that only
satisfy `reloco::collection_view_traits`/`container_ref_traits` (or custom
container types), a type-erased fallback remains available -- at the cost of
an explicit adapter call at each use site:

* `formatter<reloco::collection_view<T>>` formats a type-erased,
  vtable-backed view over any contiguous container (`reloco::vector<T>`,
  `reloco::span<T>`, `reloco::array<T, N>`, ...) as `[val1, val2, ...]`.
  `as_collection_view(container)` builds one from a container that satisfies
  `reloco::collection_view_traits`.
* `formatter<reloco::detail::mutable_sequence_container_ref<T>>` formats
  sequence container adapters the same way.
* `formatter<reloco::detail::mutable_associative_container_ref<T, Key>>`
  formats associative container adapters (e.g. `flat_set`/map adapters) as
  `{key1: val1, key2: val2, ...}`; the specifier applies to the values, not
  the keys.

```cpp
reloco::vector<int> values = ...;
microfmt::format_to(out, "{}", microfmt::as_collection_view(values));
```

See `examples/reloco_demo.cpp`.

## `grid_view.hpp`

`reg_grid_desc<WordType, N>` describes a titled, named grid of words.
`make_reg_grid(data, descriptor)` returns a `reg_grid_view` that displays the
words as aligned hexadecimal register values.

Descriptors should normally be `constexpr`:

```cpp
inline constexpr auto registers =
    microfmt::reg_grid_desc<uint32_t, 4>{
        "DMA state", 2, {"CCR", "COUNT", "PERIPH", "MEMORY"}};

microfmt::format_to(out, "{}", microfmt::make_reg_grid(dma, registers));
```

The source object must contain at least `N * sizeof(WordType)` bytes. The
formatter has no custom specifier. See `examples/grid_view_demo.cpp`.

## `math.hpp`

`vec(array)`, `vec3(x, y, z)`, and `mat<T, Rows, Cols>(flat_array)` create
vector and row-major matrix views. The complete specifier is forwarded to
every element.

```cpp
const float matrix_data[]{1, 2, 3, 4};
microfmt::format_to(out, "{:.2f}", microfmt::mat<float, 2, 2>(matrix_data));
```

The matrix factory requires a compile-time-sized array with exactly
`Rows * Cols` elements. See `examples/math_demo.cpp`.

## `monad.hpp`

`std::optional<T>` and `reloco::optional<T>` format as `Some(value)` or
`None`. `microfmt::expected<T, E>` (a `reloco::expected` alias) formats as
`Ok(value)`/`Ok()` or `Err(error)`. In C++23 builds with library support for
`<expected>`, `std::expected<T, E>` also receives a direct formatter.
`microfmt::checked_value<T>` (a `reloco::checked_value` alias) formats the
held value directly -- no wrapper, since it always holds a `T` once
constructed -- or the literal text `<moved-from>` once the value has been
moved out of.

The full specifier is forwarded to the contained value or error:

```cpp
microfmt::format_to(out, "{:04X}", std::optional<uint16_t>{0x2a});
microfmt::format_to(out, "{:04X}", microfmt::expected<uint16_t, int>{0x2a});
```

See `examples/monad_demo.cpp`.

## `boost_describe.hpp`

When Boost.Describe and Boost.MP11 are available, described enums and
structures receive automatic formatters:

* described enum values render using their declared names;
* unknown enum values fall back to their underlying numeric value;
* described public members render as `{name: value, ...}`.

The formatters accept no custom specifier.

```cpp
BOOST_DESCRIBE_STRUCT(status, (), (code, message))
microfmt::format_to(out, "{}", current_status);
```

See `examples/describe_demo.cpp`.

## Boost formatters

Boost integrations are opt-in headers: the caller must provide Boost and
include only the required formatter family.

| Header | Supported types |
|---|---|
| `boost_monad.hpp` | `boost::optional`, Boost.Variant2, Boost.Outcome results |
| `boost_containers.hpp` | `boost::container::static_vector` and `small_vector` |
| `boost_system.hpp` | Boost.System error codes and conditions |
| `boost_net.hpp` | Boost.Asio IPv4/IPv6 addresses and IP endpoints |
| `boost_values.hpp` | Dynamic bitsets, rationals, and tribools |
| `boost_time.hpp` | Boost.Chrono durations/time points and Boost.DateTime values |

Optional and result values use `Some`/`None` and `Ok`/`Err`, matching the
standard-library formatters. Container and monad element specifiers are
forwarded to contained values.

Boost.Asio addresses are rendered directly from their byte representation.
Multiprecision integers default to hexadecimal and support `x`, `X`, `b`, and
`#`; this avoids allocating a temporary decimal string.
