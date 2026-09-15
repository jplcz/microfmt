<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Presentation and compatibility

These headers change presentation without changing source ownership, or expose
microfmt through a compatibility API.

## `ansi.hpp`

`microfmt::ansi` defines foreground/background colors, attributes, combined
`style` values, semantic presets, and `styled(value, style)` wrappers.

```cpp
microfmt::format_to(out, MICROFMT_STRING("{}"),
                    microfmt::ansi::red("failure"));
```

Styles and styled views accept no custom format specifier. Set
`ansi::style::colors_enabled` at runtime to suppress ANSI escape sequences
while preserving the text. See `examples/ansi_demo.cpp`.

## `styled.hpp`

`styled_str_view` performs zero-allocation text alignment, casing, truncation,
and quoting. Factory functions include `pad`, `pad_center`, `pad_right`,
`to_upper`, `to_lower`, `truncate`, and `quoted`.

The formatter syntax is `[fill][align][width][flags]`:

| Syntax | Effect |
|---|---|
| `<`, `>`, `^` | Left, right, or center alignment |
| `u`, `l` | Uppercase or lowercase |
| `t` | Enable truncation |
| `q` | Double quotes |
| `b` | Bracket quoting |
| `.N` | Set maximum rendered length |

```cpp
microfmt::format_to(out, MICROFMT_STRING("{:*^12q}"),
                    microfmt::pad("ready", 12));
```

Factory settings provide the defaults; replacement-field flags can override
them. See `examples/styled_demo.cpp`.

## `fmt.hpp`

This header provides a lightweight `{fmt}`-style namespace backed by
microfmt:

* `fmt::format` and `fmt::format_to`;
* `fmt::format_to_n`;
* `fmt::print` and `fmt::println`;
* `fmt::join`;
* a bridge from compatible `fmt::formatter<T>` specializations into
  `microfmt::formatter<T>`.

```cpp
auto text = fmt::format<64>("value={:04X}", value);
fmt::println("{}", text.view());
```

The bridge forwards parsing and formatting to the user-provided
`fmt::formatter<T>` and does not add specifiers of its own. It is a
compatibility surface, not the upstream `{fmt}` library. See
`examples/fmt_bridge_demo.cpp`.
