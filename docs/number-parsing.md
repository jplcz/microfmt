# Number parsing

`microfmt/parsing/number_parse.hpp` (integers) and
`microfmt/parsing/number_parse_float.hpp` (floating point, opt-in) turn text
into numbers..

## Why

Printing numbers is only half the job. Parsers for config files, protocols and
command lines need the reverse, and the usual tools are awkward:

* `strtol` needs a NUL-terminated string, depends on the locale, skips leading
  spaces, and reports overflow through `errno`.
* Hand-written `value = value * 10 + digit` loops overflow silently.

These functions never allocate, never throw, ignore the locale (integers) and
tell you exactly *what* went wrong and *where*.

## Two ways to call

| Function | Behaviour | Modelled on |
|---|---|---|
| `parse<T>(text, opts)` | The **whole** text must be one number | Rust `str::parse` |
| `parse_prefix<T>(text, opts)` | Parses the longest valid **prefix**, tells you how many bytes it used | `std::from_chars` |

Use `parse` for a value you already isolated (e.g. a lexer token). Use
`parse_prefix` when the number sits inside larger text and you continue after it.

Both return `reloco::expected`: check `has_value()`, then use `value()` or
`error()` (`number_error{code, offset}`).

```cpp
#include <microfmt/parsing/number_parse.hpp>

// T is the target type; "8080" must fit in 16 bits.
auto port = microfmt::parse<std::uint16_t>("8080");
if (port.has_value()) use(port.value());

// "300" does not fit in uint8_t: error.code == number_errc::overflow,
// error.offset == index of the digit that overflowed (2).
auto bad = microfmt::parse<std::uint8_t>("300");

// Prefix parse: value 42, consumed 2; the caller continues at text[2].
auto p = microfmt::parse_prefix<int>("42px");
```

## Error codes

| Code | Meaning |
|---|---|
| `empty` | No digits (empty text, or only a sign / prefix) |
| `invalid_digit` | A byte that is not a digit of the radix, a misplaced separator, `-` on an unsigned type, or (strict `parse`) trailing text |
| `overflow` / `underflow` | Above the maximum / below the minimum of `T` |
| `invalid_radix` | `integer_options::radix` is not 0 or 2..36 |

`offset` is the byte index in the input, so a parser can turn it into a column.

## Integer options

```cpp
microfmt::integer_options opts{
    .radix = 0,          // 0: pick the base from a 0x / 0b / 0o prefix (decimal if none); or 2..36
    .allow_plus = true,  // accept "+5"
    .allow_prefix = false, // with an explicit radix, accept a matching prefix (radix 16 + "0x1f")
    .separator = '_',    // allow 1_000 (only between digits); '\0' disables
};
auto v = microfmt::parse<std::uint32_t>("0xdead_beef", opts);
```

Rules: `-` is only accepted for signed types; a leading `0` is **not** octal
(`017` is 17; use `0o17`); leading zeros are fine; no whitespace is skipped.
`-128` parses as `int8_t` but `128` is `overflow` and `-129` is `underflow`.

### Supported types

Every built-in integer type (except `bool` and plain `char`), `__int128` /
`unsigned __int128` where the compiler has them, and `reloco::fixed_int<N, Signed>`
of any width, for numbers that do not fit 64 bits:

```cpp
using u256 = reloco::fixed_int<256, false>;
auto huge = microfmt::parse<u256>("115792089237316195423570985008687907853269984665640564039457584007913129639935");
```

## Floating point (opt-in)

Include `number_parse_float.hpp` only if you need it; integer-only code never
pulls in the C library's float parser or locale.

```cpp
#include <microfmt/parsing/number_parse_float.hpp>

auto d = microfmt::parse<double>("-1.5e3");      // float, double and long double
microfmt::float_options o{
    .allow_plus = true,          // "+1.5"
    .separator = '_',            // TOML-style 1_000.5
    .allow_special = true,       // inf, infinity, nan (any case, optional sign)
    .allow_hex = true,           // 0x1.8p3
    .allow_leading_dot = true,   // ".5"
    .allow_trailing_dot = true,  // "5."
    .overflow_is_error = false,  // 1e400 -> +inf instead of an error
};
auto f = microfmt::parse<double>("inf", o);
```

How it works: the syntax is validated by microfmt (so `strtod` extras such as
leading whitespace or `nan(...)` never sneak in), the text is copied without
separators into a 1 KiB stack buffer and handed to `strtof`/`strtod`/`strtold`,
which round correctly. Consequences:

* Numbers longer than 1023 bytes return `invalid_digit`.
* The C locale's decimal point is respected internally; a multi-byte decimal
  point is unsupported (`invalid_digit`). Use the default `"C"` locale.
* Underflow to zero or a subnormal is **not** an error; overflow is, unless
  `overflow_is_error` is false.
* `1e` parses as `1` with prefix parsing (the dangling `e` is not consumed).
* The float functions are not `constexpr` (they call libc).

## Using it with the lexer

Feed the text of `integer` and `floating` tokens straight in:

```cpp
microfmt::lexer lx{src, microfmt::rules::toml};
for (auto t = lx.next(); t.kind != microfmt::token_kind::end; t = lx.next()) {
  if (t.kind == microfmt::token_kind::integer) {
    // radix 0 handles 0x/0o/0b; '_' is TOML's digit separator
    auto n = microfmt::parse<std::int64_t>(t.text, {.radix = 0, .separator = '_'});
  } else if (t.kind == microfmt::token_kind::floating) {
    auto d = microfmt::parse<double>(t.text, {.separator = '_'});
  }
}
```

C++ literal suffixes (`10UL`, `1.5f`) are the caller's job to strip first; the
engine reports them as `invalid_digit` at the suffix offset.

Demo: `examples/number_parse_demo.cpp`.
