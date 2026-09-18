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
microfmt::format_to(out, MICROFMT_STRING("{:04X}"),
                    microfmt::join(values, ", "));
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
microfmt::format_to(out, MICROFMT_STRING("{:n}"), even);
```

Use `b`, `c`, or `n` for outer delimiters. The remainder of the specifier is
forwarded to each selected element. See `examples/filter_view_demo.cpp`.

## `repeated_view.hpp`

`repeat(value, count)` and `repeat(value, count, separator)` create a view
that formats the same value `count` times, optionally joined by `separator`.

```cpp
microfmt::format_to(out, MICROFMT_STRING("{}"), microfmt::repeat('*', 5));
microfmt::format_to(out, MICROFMT_STRING("{:04x}"), microfmt::repeat(0x2A, 3, "|"));
```

The complete replacement-field specifier is forwarded to the wrapped value's
formatter and re-applied on every repetition.

## `map_view.hpp`

`map_view(container)` formats standard map-like entries as
`key: value`. Overloads accept custom key/value extractors and explicit
iterator/sentinel pairs for non-standard layouts.

```cpp
microfmt::format_to(out, MICROFMT_STRING("{}"),
                    microfmt::map_view(settings));
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
microfmt::format_to(out, MICROFMT_STRING("{:p04X}"),
                    std::tuple{1, 2, 3});
```

See `examples/tuple_demo.cpp`.

## `grid_view.hpp`

`reg_grid_desc<WordType, N>` describes a titled, named grid of words.
`make_reg_grid(data, descriptor)` returns a `reg_grid_view` that displays the
words as aligned hexadecimal register values.

Descriptors should normally be `constexpr`:

```cpp
inline constexpr auto registers =
    microfmt::reg_grid_desc<uint32_t, 4>{
        "DMA state", 2, {"CCR", "COUNT", "PERIPH", "MEMORY"}};

microfmt::format_to(out, MICROFMT_STRING("{}"),
                    microfmt::make_reg_grid(dma, registers));
```

The source object must contain at least `N * sizeof(WordType)` bytes. The
formatter has no custom specifier. See `examples/grid_view_demo.cpp`.

## `math.hpp`

`vec(array)`, `vec3(x, y, z)`, and `mat<T, Rows, Cols>(flat_array)` create
vector and row-major matrix views. The complete specifier is forwarded to
every element.

```cpp
const float matrix_data[]{1, 2, 3, 4};
microfmt::format_to(out, MICROFMT_STRING("{:.2f}"),
                    microfmt::mat<float, 2, 2>(matrix_data));
```

The matrix factory requires a compile-time-sized array with exactly
`Rows * Cols` elements. See `examples/math_demo.cpp`.

## `monad.hpp`

`std::optional<T>` formats as `Some(value)` or `None`. In C++23 builds with
library support for `<expected>`, `std::expected<T, E>` also receives a direct
formatter.

The full specifier is forwarded to the contained value or error:

```cpp
microfmt::format_to(out, MICROFMT_STRING("{:04X}"),
                    std::optional<uint16_t>{0x2a});
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
microfmt::format_to(out, MICROFMT_STRING("{}"), current_status);
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
