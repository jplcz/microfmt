# Design: number parsing engine (`microfmt/parsing/number_parse*.hpp`)

Status: **Implemented** (guide: [../number-parsing.md](../number-parsing.md); decisions in §9)

## 1. Why

microfmt can already *print* numbers; it cannot *read* them back. Every user
of the lexer (and the planned TOML/INI parsers) needs to turn the text of an
`integer` or `floating` token into a value, and today that means hand-rolled
loops (easy to get wrong on overflow, signs and separators) or `strtol`
(needs a NUL-terminated string, depends on locale, silently accepts leading
spaces and garbage). A small shared engine fixes that once.

It follows the two designs people already know:

* **Rust** `str::parse::<T>()`: the *whole* text must be a number; the error
  says why (empty, invalid digit, positive overflow, negative overflow).
* **C++** `std::from_chars`: parse a *prefix* of a buffer, report how many
  bytes were used, never throw, never allocate, never look at the locale.

### Goals

* No heap, no exceptions, `noexcept`, bounds-safe on `string_view` (no NUL needed).
* Exact overflow detection for **every** built-in integer type, `__int128`
  where available, and arbitrary-width `reloco::fixed_int<N, Signed>`.
* Options for what real formats need: radix 2..36, optional `+`, digit
  separators (`1_000`), `0x`/`0b`/`0o` prefixes (what the lexer produces).
* Floating point is **opt-in and separate**: code that does not include
  `number_parse_float.hpp` never links libm/libc float parsing and pays nothing.
* Errors carry the byte offset of the problem so a parser can report line and
  column.

### Non-goals

* Locale-aware parsing, whitespace skipping (callers trim), `bool`/`char`.
* Hand-written correctly-rounded float conversion (the float header delegates
  to the C library, §5).
* Parsing into `reloco::fixed_point` (a possible follow-up; it needs exact
  rational rounding rules and is not required by TOML/INI).

## 2. Files

| File | Contents |
|---|---|
| `include/microfmt/parsing/number_parse.hpp` | `number_errc`, `number_error`, `parsed<T>`, `integer_options`, `parse<T>`, `parse_prefix<T>` for integers |
| `include/microfmt/parsing/number_parse_float.hpp` | `float_options`, `parse<T>` / `parse_prefix<T>` overloads for `float`, `double`, `long double` |
| `docs/number-parsing.md` | User guide |
| `tests/test_number_parse.cpp`, `tests/test_number_parse_float.cpp` | Tests |
| `examples/number_parse_demo.cpp` | Demo |

The float header includes the integer header (shared error types); the integer
header never includes the float one.

## 3. Public API (integers)

```cpp
namespace microfmt {

enum class number_errc : uint8_t {
  empty,           // no characters at all (or only a sign / prefix)
  invalid_digit,   // a byte that is not a digit of the radix (or a misplaced separator)
  overflow,        // value above the type's maximum
  underflow,       // value below the type's minimum (negative overflow)
  invalid_radix,   // radix not in 2..36 and not 0
};

struct number_error {
  number_errc code;
  size_t offset;   // byte index in the input where parsing failed
};

template <typename T> struct parsed { T value; size_t consumed; };

struct integer_options {
  uint8_t radix{10};          // 2..36, or 0 = detect from the 0x / 0b / 0o prefix (decimal if none)
  bool allow_plus{true};      // accept a leading '+' (Rust does, from_chars does not)
  bool allow_prefix{false};   // accept 0x / 0b / 0o matching the radix (radix 0 implies it)
  char separator{'\0'};       // byte allowed *between* digits (e.g. '_'); '\0' disables
};

// Rust-like: whole text must be a number.
template <typename T> constexpr reloco::expected<T, number_error>
parse(string_view text, const integer_options &opts = {}) noexcept;

// from_chars-like: longest valid prefix; `consumed` is its length.
template <typename T> constexpr reloco::expected<parsed<T>, number_error>
parse_prefix(string_view text, const integer_options &opts = {}) noexcept;

}
```

`T` is any built-in integer type except `bool`/`char`-family-as-text (`char`
is accepted as an ordinary small integer only if signed/unsigned char is used
explicitly), `__int128`/`unsigned __int128`, or any `reloco::fixed_int<N, S>`.
Anything else is a compile error (constrained template).

### 3.1 Semantics

* **Sign:** `-` only for signed `T` (for unsigned it is `invalid_digit` at
  offset 0, as in Rust). `+` per `allow_plus`.
* **Prefix:** with `radix == 0` a `0x`/`0X` (hex), `0b`/`0B` (binary),
  `0o`/`0O` (octal) prefix selects the radix; no prefix means decimal (a
  leading `0` is *not* octal). With `allow_prefix` and an explicit radix the
  prefix is optional but must match the radix. A prefix with no digits after it
  is `empty`.
* **Separators:** only between two digits: not leading, trailing, doubled or
  right after a prefix.
* **Overflow:** exact for all widths. Digits accumulate into the *unsigned*
  counterpart of `T`'s width; the limit is `max` for positive and `max + 1`
  (magnitude of `min`) for negative, so `-128` parses as `int8_t` while `128`
  is `overflow` and `-129` is `underflow`. Checked with a pre-multiply bound
  `limit / radix`, no wider type needed, which is why it also works for
  `fixed_int<N>` of any width.
* **Leading zeros** are fine (`000012` is 12). Empty text is `empty`.
* **Prefix parse** stops at the first byte that cannot continue the number; a
  trailing separator is not consumed. Strict `parse` reports that byte as
  `invalid_digit` at its offset.
* No whitespace skipping, no locale, no `errno`.

## 4. Public API (floating point, opt-in)

```cpp
struct float_options {
  bool allow_plus{true};          // leading '+'
  char separator{'\0'};           // byte allowed between digits (TOML '_')
  bool allow_special{false};      // inf, infinity, nan (case-insensitive, optional sign)
  bool allow_hex{false};          // C99 hex floats 0x1.8p3
  bool allow_leading_dot{true};   // ".5"
  bool allow_trailing_dot{true};  // "5."
  bool overflow_is_error{true};   // false: out-of-range yields +/-infinity like Rust
};

template <typename T> /* float, double, long double */
reloco::expected<T, number_error> parse(string_view text, const float_options & = {}) noexcept;
template <typename T>
reloco::expected<parsed<T>, number_error> parse_prefix(string_view text, const float_options & = {}) noexcept;
```

Grammar (decimal): `[sign] digits [. digits] [(e|E) [sign] digits]` with the
dot/exponent rules from the options. Underflow to zero or a subnormal is **not**
an error (value is returned), like Rust; overflow is `overflow` unless
`overflow_is_error` is false.

## 5. Floating point implementation

Correct rounding is hard; the C library already does it right
(`strtof`/`strtod`/`strtold`), so we use it but wrap the awkward parts:

1. **We validate the syntax ourselves** against the grammar above (so
   `strtod`'s extras — leading whitespace, `0x`, `inf`, `nan(...)` — only get
   through when the options allow them) and compute the exact prefix length.
2. We copy the validated text, minus separators and with `+` removed, into a
   **bounded stack buffer (1024 bytes)** and NUL-terminate it, because
   `strto*` needs a C string. Longer numbers return `number_errc::invalid_digit`
   at offset 1024 (documented limit; real numbers are far shorter).
3. The decimal point is replaced by the current C locale's single-byte decimal point when it is not `.`; a
   multi-byte point returns `invalid_digit`.
4. `errno` is cleared before and read after the call; `ERANGE` with an infinite
   result is `overflow`; subnormal/zero results are accepted.
5. Hex floats and special values are handled by `strto*` after our validation.


Bare-metal note: the float header is the only part needing a hosted libc
(`<cstdlib>`, `<cerrno>`, `<clocale>`); it is not included by anything else.

## 6. Integration

* The lexer's `integer` / `floating` tokens feed straight in:
  `parse<int64_t>(tok.text, {.radix = 0, .separator = '_'})` for
  `rules::toml`/`c` tokens (a C++ suffix such as `UL` is the parser's job to
  strip first; the engine reports it as `invalid_digit` at its offset).
* The TOML/INI parsers (next design) use `parse<int64_t>` and `parse<double>`.
* Errors format with the usual `{}` (`number_error` gets a `formatter`).

## 7. Safety and reloco conventions

* Pure `constexpr`/`noexcept` functions over `string_view`; no raw pointers
  (the float buffer is a `reloco::array<char, 1024>`; index access is bounded).
* Results are `reloco::expected` (value/error), no out-parameters.
* `[[nodiscard]]` everywhere; no allocation; `parsed<T>` is trivially copyable.
* `-Wunsafe-buffer-usage` clean: the only unsafe spots are the libc call
  (`RELOCO_BEGIN/END_UNSAFE_BUFFER_USAGE` with a bound comment).

## 8. Tests

* Per type (`int8_t`…`uint64_t`, `__int128`, `fixed_int<256>` signed/unsigned):
  min/max round trip, `max+1` overflow, `min-1` underflow, `-0`.
* Radix 2, 8, 10, 16, 36; letters of both cases; prefix detection; prefix with
  no digits; separators accepted/rejected positions; leading `+`.
* Strict vs prefix: consumed count, `invalid_digit` offsets, trailing garbage.
* Floats: exact decimal cases (`0.1`, `1e-320` subnormal, `1e400` overflow,
  `float` vs `double` rounding of the same text), options (specials, hex,
  dots, separator), prefix lengths (`1.5abc`, `1e` → consumes `1`), buffer limit.
* Round trip with microfmt's own float formatter for a sample of values.

## 9. Decisions

| Question | Decision |
|---|---|
| API shape | Both: strict `parse<T>` (Rust) and `parse_prefix<T>` (`from_chars`) |
| Float algorithm | Delegate to C `strtof`/`strtod`/`strtold` (user: C functions are fine) behind our own syntax validation and a bounded buffer; no hand-rolled rounding |
| Integer options | `integer_options`: radix 2..36 or 0 (detect), `allow_plus`, `allow_prefix`, `separator` |
| Widths | All built-in integers, `__int128` where available, `reloco::fixed_int<N, S>` for large numbers |
| Float separation | Own header, opt-in; integer header does not depend on it |
| `fixed_point` parsing | Deferred; not needed for TOML/INI |
