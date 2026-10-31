<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Cheatsheet: core and value formatters

Part of the [formatter cheatsheet](../cheatsheet.md). All outputs below were produced by compiling/running real code (g++ -std=c++20; 128-bit with `-std=gnu++20`).
`[..]` in examples = exact output. `ns` = `microfmt::`. `ESC` = `\x1b`.

---------------------------------------------------------------------------
## PART 1 — CORE (`<microfmt/microfmt.hpp>`, `microfmt.ipp`)
---------------------------------------------------------------------------

## Format-string syntax (implemented in `vformat_to` / `compile_format_string`)

| Syntax | Behaviour (verified) |
|---|---|
| `{}` | next auto arg (auto counter starts at 0) |
| `{N}` / `{N:spec}` | zero-based explicit index; digits only (`{01}` == `{1}`). Explicit fields do NOT advance the auto counter: `"{} {0} {}"`(1,2) -> `[1 1 2]`; `"{1} {}"`(1,2) -> `[2 1]` |
| `{:spec}` | everything after the FIRST `:` up to the first `}` is the spec, handed verbatim to `formatter<T>::parse` |
| `{{` / `}}` | literal `{` / `}`: `"{{}} {{{}}}"`(1) -> `[{} {1}]` |
| missing arg (`{5}`, or too few args) | writes `{MISSING}` (runtime path). With `MICROFMT_STRING` an out-of-range explicit `{N}` is a compile error (`std::get`). |
| extra args | silently ignored |
| non-numeric "index" (`{name}`, `{ }`, `{-1}`, `{ 0}`, `{x:5}`) | NOT an error: treated as an ordinary AUTO field (`{x:5}`(1) -> `[    1]`); text before `:` is ignored. No named args. |
| unclosed `{` (`"{"`, `"a{"`, `"{:5"`) | emitted literally; lone `}` is literal |
| dynamic width/precision `{:{}}` | unsupported: spec is `{` (up to first `}`), remaining `}` printed literally: `"{:{}}"`(1,2) -> `[1}]` |
| spec containing `}` | impossible (first `}` terminates the field) |

Formatter lookup priority for plain `{}`/`{:spec}`: explicit `formatter<T>` > `reloco::Display<T>` > `reloco::Debug<T>`; Display/Debug ignore the spec entirely (`{:?}` on a Display-only type uses **Display**; only `as_debug` prefers Debug). Argument types are `std::decay`ed; `const char[N]` lengths are collapsed (no per-literal code bloat). `cv/ref` are stripped.

## Integers — `formatter<T>` for every `std::is_integral` T except `bool`, `char`
Covers `signed/unsigned char` (printed as NUMBERS), `short`..`long long`, `wchar_t`, `char8_t/16_t/32_t` (numbers), and `__int128`/`unsigned __int128` **only when `std::is_integral` is true (GNU mode `-std=gnu++NN`; false with strict `-std=c++NN` → "incomplete type" error)**. `sizeof(T) <= 8` goes through int64/uint64; wider uses the generic path (48-byte buffer).

- **Spec grammar:** `[#][0][width][b|x|X]` (the `#` and `0` flags may come in either order)
  - `#` alt form → `0x`/`0X`/`0b` prefix, **only meaningful with b/x/X** (`{:#}`(255) -> `[255]`)
  - `0` zero-pad flag (`{:0#x}` and `{:#0x}` are equivalent: `{:0#10x}`(255) -> `[0x000000ff]`)
  - `width` decimal digits, stored in `uint16_t` and saturating at 65535 (`{:300}` = 300 wide). Width counts prefix+sign+digits.
  - `b` binary (`{:08b}`(5) -> `[00000101]`, `{:#b}`(5) -> `[0b101]`); `x`/`X` hex lower/upper; any other trailing char (`o`, `d`, `+`, `?`) is **silently ignored → decimal**. Text after the type char ignored (`{:x5}` -> `[ff]`). Wide (`reloco::fixed_int`) integers do not support `b`.
  - NOT supported: `<`/`>`/`^`/fill, `+`/space sign, `o`, `d`, `,`.
- Spaces pad on the left (right-aligned) unless `0`; zero pad goes after sign/prefix.
- **Negative numbers in hex/binary are printed sign+magnitude (like `std::format`), not two's complement**: `{:b}`(int8_t(-3)) -> `[-11]`, `{:x}`(-1) -> `[-1]`, `{:x}`(-255) -> `[-ff]`, `{:#x}`(-42) -> `[-0x2a]`, `{:08x}`(int8_t(-1)) -> `[-0000001]`. Use `ns::hex(v)` (sign-extends to 64 bit) or cast to unsigned for bit patterns.
- **Example:**

```cpp
ns::format<64>("{:5}|{:05}|{:05}|{:#06x}|{:#X}|{:6x}", 42, 42, -42, 255, 255, 255)
// -> [   42|00042|-0042|0x00ff|0XFF|    ff]
```
(For `{:o}`, `{:d}`, `{:+}`: output is the plain decimal.)

## `char`
- **Spec:** `[?]` (debug flag; `MICROFMT_DISABLE_DEBUG_FLAG` removes it). Width etc. ignored (`{:5}`('A') -> `[A]`).
- `{}`('A') -> `[A]`; `{:?}` -> `'A'`, `'\n'`, `'\''` (quote escaped), `'"'` (double quote NOT escaped), `'\x01'`, `'\xc3'` (bytes ≥0x7f → `\xHH` lowercase).
- Escape set (shared `detail::write_escaped_char`): `\0 \a \b \t \n \v \f \r \\`, quote char (if escaped), printable 0x20–0x7e verbatim, else `\xHH`.

## `bool`
No spec accepted (ignored, no width): `true` / `false`. `{:5}`(true) -> `[true]`.

## Strings: `ns::string_view`, `std::string_view`, `const char*`, `char*`, `char[N]`, `const char[N]`
- **Spec:** `[?]` only. **No width/fill/precision** (`{:5}`("ab") -> `[ab]`, `{:.1}` -> `[ab]`) — use `ns::as_string(...)` (string.hpp) or `ns::pad(...)` (styled.hpp).
- `{:?}` wraps the text in double quotes and escapes `\`, double quote, control characters and `\xHH` for bytes >= 0x80 (no UTF-8 awareness). A single quote is NOT escaped.
- `const char*`/`char*` null → `(null)` (also `{:?}` → `"(null)"`). C strings stop at first `\0`; arrays are decayed to `const char*` (so `"a\0b"` prints `a`).
- `std::string` needs `<microfmt/formatters/string.hpp>`; wide strings unsupported.

## Raw pointers (`T*` except `char`/`const char*`) and `std::nullptr_t`
- **Spec:** `[0]digits` — optional leading `0`, then width digits (width = minimum **hex digits**, always zero-padded). Anything else (`x`, `p`, `#`, `?`) is ignored. (`int` width, no cap.)
- Output `0x` + lowercase hex, no `0x` padding counted: `(void*)0x1234` -> `[0x1234]`; `{:8}` / `{:08}` -> `[0x00001234]`; `{:4}`(0xabc) -> `[0x0abc]`; `(void*)0` -> `[0x0]`; `(void*)0` with `{:4}` -> `0x0000`.
- `std::nullptr_t` → `0x0` (spec ignored; `{:10}` -> `[0x0]`). Function pointers/member pointers have no formatter (see `pointer.hpp` for `raw_ptr`).
- Pointers to `char` print as C strings, not addresses; cast to `const void*` for the address.

## `{:?}` summary
Consumed (`?` must be FIRST char of spec) by: `char`, `string_view`, `std::string_view`, `const char*`/`char*`/`char[N]`. Everything else ignores it (integers, bool, pointers, floats print as `{}`); `ns::as_string` accepts `?` too (escaped differently, see string.hpp).

## `ns::as_debug` — force Debug priority
- **Include:** microfmt.hpp. Returns a `detail::debug_view` over the value (holds a reference). Priority: `reloco::Debug` of the type, then its `formatter` (spec parsed normally), then `reloco::Display`; static_assert if none.
- **Rvalues are deleted** (`as_debug(1.0)`, `as_debug(std::byte{42})` do not compile → bind to a variable first).

```cpp
dd d{3};  // has Display (prints D3) and Debug (prints Dbg3)
ns::format<32>("{}|{:?}|{}", d, d, ns::as_debug(d))  // -> [D3|D3|Dbg3]
std::string_view s="sv\n"; ns::format<32>("{}", ns::as_debug(s))  // -> ["sv\n"]   (formatter's own ?-mode)
int v=255; ns::format<32>("{:x}", ns::as_debug(v))    // -> [ff]  (no Debug<int> → formatter<int>, spec forwarded)
```
- A spec given to a `reloco::Debug` or `reloco::Display` specialization is ignored: `{:x}` on `as_debug(d)` -> `[Dbg3]`.

## Output API (all noexcept, never allocate unless a container/pmr sink is used)

| Function | Header | Notes |
|---|---|---|
| `ns::format<N>(fmt, args...) -> buffer_sink<N>` | microfmt.hpp | inline `char[N]`, **silently truncates** at N (no `\0`). Returns by value; `.view()` on the temporary is `= delete` → `auto r = format<64>(...); r.view();` |
| `ns::format_to(const sink& out, fmt, args...)` | microfmt.hpp | core; `fmt` is runtime `string_view` OR `MICROFMT_STRING("...")` |
| `ns::format_to(OutputIt it, fmt, args...) -> OutputIt` | microfmt.hpp | any output iterator / `char*` (unbounded!) / `std::back_inserter(str)` |
| `ns::format_as<Container>(fmt, args...)` (e.g. `<std::string>`) | microfmt.hpp | uses `back_inserter`; may throw bad_alloc; runtime or `MICROFMT_STRING` |
| `ns::format_args` / `ns::make_format_args(args...)` | microfmt.hpp | type‑erased, borrows args (must outlive use; only valid within the full expression for temporaries) |
| `ns::vformat_args_to(sink, fmt, format_args)` / `vformat_args_as<Container>(fmt, args)` | microfmt.hpp | non-template boundary |
| `ns::vformat_to(sink, fmt, span<const void*const>, span<const format_fn_t>)` | microfmt.hpp | raw engine |
| `MICROFMT_STRING("...")` | microfmt.hpp | compile-time parse + unrolled dispatch for format_to/format/format_as/print/println; under `MICROFMT_SHARED` degrades to runtime. `{:?}` etc. identical. |
| `ns::print / println(fmt, args...)` | `sinks/stdio.hpp` | stdout. Overloads taking first arg `std::FILE*`, `int fd` (POSIX), or `ns::sink`; `println()` / `println(sink)` / `println(FILE*)` / `println(fd)` emit only `\n`. (Does not flush.) |
| `stdout_sink()`, `stderr_sink()`, `file_sink(FILE*)`, `fd_sink(int)` | `sinks/stdio.hpp` | return `ns::sink` |
| `ns::format_to_container(c, fmt, args...)`, `format_as_container<C>`, `make_container_sink(c)`, `container_sink<C>` | `sinks/container_sink.hpp` | growable char containers (std::string, vectors, …); throws bad_alloc if exceptions on |
| `ns::memory_buffer<InlineCap=512>` | `sinks/memory_buffer.hpp` | inline then allocator; `.as_sink()`, `.data()`, `.size()`, `push_back` (back_inserter-able) |
| others in `sinks/`: `ring_buffer_sink<2^k>`, `tee_sink`/`make_tee`, `c_string_span_sink`, `pmr_sink`/`arena_sink`, `sbuf_sink`, `styled_sink.hpp` (`transform_sink`,`prefix_sink`,`limit_sink`), syslog/systemd/android/dlog/asio sinks | | see their headers |

Sink types in microfmt.hpp (all expose `.as_sink()` → `ns::sink`; the sink object must outlive the `sink` handle):

| Type | Behaviour |
|---|---|
| `ns::sink` (= `reloco::sink{void* ctx, void(*)(void*, string_view) noexcept}`) | type‑erased; `.put(char)`, `.write(string_view)` |
| `span_sink(span<char>)` | truncating; `.view() .size() .available() .reset()`; no NUL |
| `buffer_sink<N>` | as `format<N>` result; `.view() .size() .capacity() .available() .as_span() .reset()` |
| `c_string_sink<N>` | keeps `\0`, max payload N‑1; `.c_str() .view() .size() .max_size()`. `c_string_sink<8>` + `"abcdefghij"` → `abcdefg`, size 7 |
| `iterator_sink<It>` | forwards to output iterator; `.current()` |
| `counting_sink` | `.count()` bytes (`{:08x}`(1) → 8) |
| `null_sink::as_sink()` | discards |
| `callback_sink<F>` / `make_callback_sink(F&)` | `F(string_view)` |

Verified examples:

```cpp
char store[8]; ns::span_sink ss{ns::span<char>{store, 8}};
ns::format_to(ss.as_sink(), "state={}", "ready");     // ss.view() == "state=re" (truncated)
char raw[16]; char* end = ns::format_to(raw, "n={}", 12);   // raw[0..end) == "n=12"
std::string s = ns::format_as<std::string>("{}-{}", 1, "z");  // "1-z"
ns::format<64>(MICROFMT_STRING("id={}, hex={:#x}"), 7, 255)   // [id=7, hex=0xff]
ns::format<64>(MICROFMT_STRING("{1} {0}"), 7, 255)            // [255 7]
int cc=3; ns::buffer_sink<32> vb; ns::vformat_args_to(vb.as_sink(), "{} {}", ns::make_format_args(cc, "s"));  // [3 s]
```

## Custom formatter protocol (`template<> struct ns::formatter<T>`)
`constexpr void parse(format_parse_context&) noexcept;` + `void format(const T&, const sink&) const noexcept;`. `format_parse_context`: `.spec()`, `.empty()`, `.size()`, `.front()/.back()/[i]` (return `'\0'` OOB), `.consume()`, `.starts_with(c|sv)`, `.find`, `.substr`, `.remove_prefix(n)`, `.advance_to(it)`, `begin/end`, `.consume_debug_flag()`. A `fmt::formatter<T>` with `parse(fmt::format_parse_context&)`+`format(T, fmt::format_context&)` is auto-bridged once `formatters/fmt.hpp` is included. `detail::element_formatter<T>` is what containers use for elements (formatter > Display > Debug).

---------------------------------------------------------------------------
## PART 2 — FORMATTERS
---------------------------------------------------------------------------

## `floating.hpp` — `float`/`double`/`long double` via snprintf
### `formatter<float>`, `formatter<double>`, `formatter<long double>`
- **Include:** `<microfmt/formatters/floating.hpp>` (float is promoted to double; `long double` uses `%Lg`).
- **Spec (strict order):** `[+| |-][#][0][width][.N][f|F|e|E|g|G|a|A]`

| Token | Meaning |
|---|---|
| `+` / ` ` | force `+` / leading space for non‑negative. `-` accepted and ignored |
| `#` | alternate form (`3.` for `{:#.0f}`, `3.00000` for `{:#g}`) |
| `0` | zero-pad up to `width`, after the sign (`{:08.2f}`(-3.14159) -> `-0003.14`) |
| `width` | minimum field width, right-aligned (`{:8.2f}` -> `    3.14`); no fill/alignment options |
| `.N` | precision; `.` alone = precision 0 (`{:.}` -> `3`); digits unbounded (int, may overflow) |
| type | f F e E g G a A; **default `g`** (`{}` = `%g` → 6 sig. digits). Unknown/other letters ignored (`{:.2lf}` ignored `l`→ `g`!) |

- **No fill or alignment characters** (`{:<8.2f}` is not accepted: the spec is discarded and the value prints with defaults). Width is always right-aligned; for other alignment pad with `ns::align`.
- **Examples (d = 3.14159265):**

```cpp
{} [3.14159]  {:f} [3.141593]  {:.2f} [3.14]  {:.0f} [3]  {:.2} [3.1]  {:e} [3.141593e+00]  {:.3E} [3.142E+00]
{:+.1f} [+3.1]  {: .1f} [ 3.1]  {:-.1f} [3.1]  {:#.0f}(3.0) [3.]  {:g}(1e10) [1e+10]  {:a}(1.0) [0x1p+0]  {:A} [0X1P+0]
{:.2f}(-0.0) [-0.00]  {:f}(NAN) [nan]  {:F}(INFINITY) [INF]  {:8.2f} [    3.14]  {:?} [3.14159]
```
- **Notes:** `{:?}` ignored (use `as_debug` + debug_float.hpp). Uses 128‑byte stack buffer, **falls back to `new (std::nothrow)`** for longer output (e.g. `{:.200f}` or `{:.1f}`(1e300)) → heap use; allocation failure silently drops output. Locale‑dependent decimal point possible (snprintf). Not available in kernel/FPU‑less builds by design.

## `debug_float.hpp` — `reloco::Debug<float/double/long double>`
- **Include:** `<microfmt/formatters/debug_float.hpp>` (not pulled in by anything else; pair with floating.hpp). Namespace `reloco`.
- **Spec:** none — only reachable via `ns::as_debug(lvalue)` (a plain `{:?}` on a double still uses `formatter<double>` and prints `%g`). Spec given with as_debug is ignored (`{:.2f}`(as_debug(1.0)) -> `[1.0]`).
- Format: `%g` (`%Lg`), 64‑byte buffer, appends `.0` if there is no `.`/`e`/`E` and not inf/nan.

```cpp
double d1=1.0; float f1=3.5f;   as_debug(d1) [1.0]  as_debug(f1) [3.5]  1e20 [1e+20]  0.1 [0.1]  NAN [nan]  -INFINITY [-inf]  1e-7 [1e-07]  1.0L/3 [0.333333]
```
- **Notes:** `{}` of `as_debug(double)` is `%g` (6 digits) — not round‑trip precise. **docs' `as_debug(1.0)` (rvalue) does not compile.**

## `string.hpp` — `std::basic_string` + `as_string`
### `formatter<std::basic_string<CharT,Traits,Alloc>>`
- No spec accepted/ignored (`{:?}`, `{:10}` print plain). Meant for `char` strings (writes raw bytes).
### `as_string(...)` → `as_string_view`
- **Include:** `<microfmt/formatters/string.hpp>`
- **Factories:** `as_string(ns::string_view)`, `as_string(const std::basic_string<...>&)` (rvalue string overload **deleted**), `as_string(const char*)` (null → `(null)`). `std::string_view` converts to `ns::string_view`. Borrows text.
- **Spec:** `[[fill]align][width][.precision][?]`

| Token | Meaning |
|---|---|
| `fill` | any one char, only recognised when the *second* char is `<`/`>`/`^` |
| `<` `>` `^` | left / right / center (center: extra pad goes to the RIGHT) |
| `width` | min field width (size_t); if width>0 and no align → left |
| `.N` | truncate to N source bytes BEFORE escaping/padding (`.0` → empty) |
| `?` | anywhere after precision (any trailing `?` char): debug-quote `"..."` |
| other trailing chars (`s`, `x`) | ignored |
- Debug escaping here is **different from `{:?}` on plain strings**: only `\n \r \t \\ backslash-quote \0`, and `\xHH` for <0x20 and 0x7F; `'` and non-ASCII bytes verbatim. Width counts the *escaped* length incl. quotes.

```text
as_string("hello world") with: {:15}[hello world    ] {:>15}[    hello world] {:^15}[  hello world  ] {:*^15}[**hello world**]
{:-<15}[hello world----] {:.5}[hello] {:10.3}[hel       ] {:*>12.8?}[**"hello wo"] {:0>5}("7")[00007] {:^5}("ab")[ ab  ]
{:?}("a\"b'\n\t\r\\\x01\x7f\xc3\xa9") -> ["a\"b'\n\t\r\\\x01\x7fé"]   {:>10?}("a\n") [     "a\n"]   as_string(nullptr) [(null)]
```
- **Gotcha:** `[fill]align` is detected only at the very start: `{:5<}` is parsed as fill=`'5'`, align=left (no width!). Always write fill/align BEFORE width (`{:<5}`, `{:*<5}`). A fill char that is a digit works only when followed by an align char (`{:0>5}` → `00007`).

## `styled.hpp` — `styled_str_view` (pad / case / truncate / quote)
- **Include:** `<microfmt/formatters/styled.hpp>`; `enum class text_case{none,upper,lower,title}`, `text_align{left,center,right}`, `quote_style{none,double_quotes,single_quotes,brackets,parens,angle_brackets}`.
- **Struct:** `styled_str_view{ string_view text; size_t width=0; char fill_char=' '; text_align align=left; text_case casing=none; quote_style quote=none; size_t max_len=0 (0=off); bool ellipsis=true; }` (aggregate; `styled_str_view{"abc"}` OK). Borrows text.
- **Factories (constexpr):**
  - `pad(string_view s, size_t width, text_align a = left, char fill = ' ')`
  - `pad_center(s, width, fill=' ')`, `pad_right(s, width, fill=' ')`
  - `to_upper(s)`, `to_lower(s)` (ASCII only)
  - `truncate(s, size_t max_chars, bool use_ellipsis=true)` — **call `microfmt::truncate`; unqualified `truncate("..",6)` with `using namespace microfmt` binds POSIX `::truncate` (returns -1)**
  - `quoted(s, quote_style q = double_quotes)`
- **Spec:** `[[fill]align][width][flags...]`; flags may be combined in any order after width.

| Token | Meaning |
|---|---|
| `fill` + `<`/`>`/`^` | override alignment; **an align char also overrides the factory's fill with the spec's fill (default `' '`)** |
| `width` | override width (only if >0) |
| `u`/`U`, `l`/`L`, `t`/`T` | upper / lower / **title case** (first letter, and letters after space or `_`; rest lowercased). (`t` is NOT "truncation" as docs say) |
| `q` | double quotes `"..."`; `b` → `[...]`. (single/paren/angle only via factory) |
| `.N` | max length N (needs a digit‑char after `.` to exist); with ellipsis `...` when N>3 else hard cut; `.0`=off |
| other chars | ignored |
- Truncation: if `text.size() > max_len`: ellipsis & max_len>3 → first `max_len-3` chars + `...` (total = max_len); else first `max_len` chars. Quotes/ellipsis counted in width; padding outside quotes.

```cpp
pad("ab",6) [ab    ]  pad_right("ab",6) [    ab]  pad_center("ab",7,'*') [**ab***]  pad("ab",6,right,'.') [....ab]
to_upper("Hello wOrld") [HELLO WORLD]  microfmt::truncate("abcdefghij",6) [abc...]  truncate(..,6,false) [abcdef]  truncate("abc",6) [abc]
quoted("x") ["x"] quoted("x",single_quotes) ['x'] brackets [[x]] parens [(x)] angle_brackets [<x>]
{:*^12q}(pad("ready",12)) [**"ready"***]   {:>10}(styled_str_view{"abc"}) [       abc]   {:t}("hello big_world") [Hello Big_World]
{:.5}("abcdefghij") [ab...]   {:.2} [ab]   {:*<10q.6u} [ "ABC..."** ]   {:^9bu}("abc") [  [ABC]  ]   {:8}(pad("ab",4,right,'.')) [......ab]
{:>10}(pad("ab",6,left,'.')) [        ab]   <- spec's align resets fill to ' '
```

## `std_types.hpp` — std interop display formatters
- **Include:** `<microfmt/formatters/std_types.hpp>` (includes string.hpp).

| Type | Spec | Output |
|---|---|---|
| `std::filesystem::path` | forwarded to `formatter<std::string>` (= none) | native text, allocates via `path::string()`: `[/etc/hosts]` |
| `std::bitset<N>` | exactly `#` → `0b` prefix; anything else ignored | MSB first: `{}`(5,N=4) `[0101]`; `{:#}` `[0b0101]`; `{:x}` `[0101]` |
| `std::byte` | forwarded to `formatter<unsigned>` (`[#][0][width][xX]`) | `{}`(7) `[7]`; `{:02x}`(10) `[0a]`; `{:#x}`(255) `[0xff]` (`{:08b}` -> `00001010` for 10) |
| `std::ratio<N,D>` | none | reduced `num` or `num/den`: `[1/1000]`, `[3]`, `std::ratio<-2,6>` `[-1/3]` |

## `std_complex.hpp` — `std::complex<floating>`
- **Include:** `<microfmt/formatters/std_complex.hpp>` (pulls floating.hpp). **Spec:** whole spec forwarded to the float formatter for BOTH parts (so floating.hpp rules, no width).
- Output `(re±imi)` (sign taken from `signbit` of imag): `{}`(1.5,2.25) `[(1.5+2.25i)]`; `{:.2f}`(1.5,-2.25) `[(1.50-2.25i)]`; `{:e}`(complex<float>(1,0)) `[(1.000000e+00+0.000000e+00i)]`; `(0,-0.0)` `[(0-0i)]`.

## `std_debug.hpp` — `reloco::Debug` for std types (use via `ns::as_debug(lvalue)`)
- **Include:** `<microfmt/formatters/std_debug.hpp>`. No spec.

| Type | Output |
|---|---|
| `std::array<T,N>` | `[e1, e2]`, each element via as_debug: `{1,2,3}` → `[1, 2, 3]`; `array<string_view,2>{"a","b"}` → `[a, b]`; `array<int,0>` → `[]` |
| `std::bitset<N>` | bits MSB first, no prefix: `bitset<6>(5)` → `000101` |
| `std::reference_wrapper<T>` | forwards to `as_debug(get())` → `5` |
| `std::byte` | `0x2a` (always 2 hex digits, lowercase) |

(String elements are NOT quoted: nested `as_debug(string_view)` uses `formatter<string_view>` with an empty spec, i.e. no `?`. `reference_wrapper<int>` → `5`, `bitset<6>(5)` → `000101`, `byte{42}` → `0x2a`. Example: `ns::format<32>("{}", ns::as_debug(arr))` with `std::array<int,3> arr{1,2,3}` → `[1, 2, 3]`.)

## `numeric_views.hpp` — digit grouping
### `thousands(T value, char separator=',', uint8_t group_size=3)` → `thousands_view<T>`
- **Include:** `<microfmt/formatters/numeric_views.hpp>`. T integral, non‑bool (SFINAE). Spec: none (`{:x}`/width ignored).

```cpp
thousands(1234567) [1,234,567]  thousands(-1234567) [-1,234,567]  thousands(1234567,'_') [1_234_567]  thousands(12345678,' ',4) [1234 5678]
thousands(0) [0]  thousands(INT64_MIN) [-9,223,372,036,854,775,808]  thousands(UINT64_MAX) [18,446,744,073,709,551,615]
thousands(1234567,',',0) [1234567]  thousands('A') [65]  thousands(255) with {:x} [255]
```
- Notes: values ≤64 bit only (cast to uint64). `group_size 0` disables grouping.

## `fixed_point.hpp` — scaled integers as decimals (truncating, no float)
- **Include:** `<microfmt/formatters/fixed_point.hpp>`
- **Types/factories:** `fixed_point_view<uint64_t Scale, uint8_t Decimals=2, typename IntType=int32_t>{IntType raw_value}` (`Scale>0`, `Decimals<=18`, IntType integral — static_asserts); `fixed<Scale, Decimals=2>(IntType raw)`; aliases/factories `milli(raw)` (1000,3), `centi(raw)` (100,2), `micro(raw)` (1000000,6) + `milli_view<T>/centi_view<T>/micro_view<T>`.
- **Spec:** none (width etc. ignored: `{:8}` -> `[0.001]`).
- Output `[-]int.frac` with exactly `Decimals` digits (omit `.` when Decimals==0). Fraction **truncated** (not rounded). Sign suppressed if value rounds to zero magnitude (e.g. raw -0 → no `-`).

```cpp
milli(3300) [3.300]  milli(-5) [-0.005]  milli(-1234) [-1.234]  centi(7) [0.07]  micro(1500000) [1.500000]  fixed<10,1>(25) [2.5]
fixed<256,4>(uint16_t(300)) [1.1718]  fixed<100,0>(250) [2]  fixed<3,3>(10) [3.333]  milli(int8_t(-100)) [-0.100]
milli<int64_t>(-1) [-0.001]  milli(INT32_MIN) [-2147483.648]  milli(int64_t(INT64_MIN)) [-9223372036854775.808]
```
- Notes: `milli` etc. deduce IntType from arg (`milli(3300)` → int). `Decimals>log10(Scale)` pads zeros (frac*10^D/Scale). Frac multiplication may overflow uint64 for huge Scale·10^Decimals.

## `base_views.hpp` — base64, grouped binary
- **Include:** `<microfmt/formatters/base_views.hpp>` (includes binary.hpp). Ships `.ipp` (out-of-line under MICROFMT_SHARED).
### `base64(span<const uint8_t>)`, `base64(const uint8_t(&)[N])` → `base64_view` (borrows)
- **Spec:** none. Standard alphabet, `=` padding, no line wrapping. `base64("Man")` `[TWFu]`, 2 bytes `[TWE=]`, 1 byte `[TQ==]`, empty `[]`.
- `base64(const char[])`/std::string not accepted; use `span<const uint8_t>(ptr, size_t{n})` (note: `span(p, 3)` with int literal is ambiguous → use `size_t{3}`).
### `bin_grouped(T val, uint8_t group, char sep='_')` → `bin_grouped_view<T>`
- T integral non-bool. **Spec:** none. Always full bit‑width of T, `0b` prefix, separator every `group` bits counted from the LSB (0 = no separators).

```cpp
bin_grouped(uint8_t(0xA5),4) [0b1010_0101]  bin_grouped(uint16_t(0xA53C),8,'.') [0b10100101.00111100]  bin_grouped(uint8_t(5),0) [0b00000101]  bin_grouped(int8_t(-2),3) [0b11_111_110]
```
- Note: `bin_grouped(val)` (1 arg) is binary.hpp's `binary_view` factory (no prefix, nibble groups) — different type, same name; overload picked by arity.

## `binary.hpp` — `binary_view`
- **Include:** `<microfmt/formatters/binary.hpp>`
- **Factories (T integral non-bool):** `bin(T v)` (full width, no prefix), `bin<uint8_t Bits>(T v)` (min bits), `bin_prefixed(T v, bool group_nibbles=false)` (`0b`), `bin_grouped(T v)` (nibbles `_`). View: `binary_view<T>{T value; uint8_t min_bits=sizeof(T)*8; bool prefix=false; bool group_nibbles=false;}`.
- **Spec:** characters scanned anywhere: `#` → `0b` prefix, `_` → nibble groups with `_`. Everything else (width, `0`, `x`) ignored. View flags and spec flags are OR‑ed.
- Always zero‑pads to `min_bits`; `Bits` < value width truncates high bits (`bin<3>(0xff)` → `111`); `Bits==0` → full width; `Bits` > type width prints leading zeros (but shift ≥ width is UB for `uint32/uint64` — keep Bits ≤ width).

```cpp
bin(uint8_t(42)) [00101010]  bin<4>(uint8_t(5)) [0101]  bin<12>(uint16_t(0xa53c)) [010100111100]  {:#}(bin(uint8_t(42))) [0b00101010]
{:_} [0010_1010]  {:#_}(bin<16>(uint16_t(0xa53c))) [0b1010_0101_0011_1100]  bin_prefixed(uint8_t(42),true) [0b0010_1010]  bin(5) [00000000000000000000000000000101]  bin(int8_t(-1)) [11111111]
{:#_}(bin<6>(uint8_t(0x2a))) [0b10_1010]
```
- `bin(x)` (one argument) is binary.hpp's overload; `format_helpers.hpp` includes binary.hpp and adds `bin(x, width, prefix)`, so including both headers is fine.

## `format_helpers.hpp` — diagnostic adapters
- **Include:** `<microfmt/formatters/format_helpers.hpp>`. **None of these views accept a spec** (all ignored).
### `hex(T val, int width=0, bool prefix=false, bool uppercase=false)` → `hex_view<T>`
- `static_cast<uint64_t>(val)` → **negatives sign‑extend**: `hex(-1)` `[ffffffffffffffff]`, `hex(int8_t(-1))` same, `hex(uint8_t(255))` `[ff]`. `hex(255,4)` `[00ff]`; `hex(255,4,true)` `[0x00ff]`; `hex(255,4,true,true)` `[0X00FF]` (digits uppercase too); width counts digits only (zero pad); `hex('A')` `[41]`.
### `bin(T val, int width, bool prefix=false)` → `bin_view<T>`
- `bin(5,32)` `[000…0101]` (32 digits), `bin(uint8_t(5), 8)` `[00000101]`, `bin(5,8,true)` `[0b00000101]`, `bin(uint8_t(5),0)` `[101]`, `bin(0,1)` `[0]`, `bin(int8_t(-2), 8)` → `[11111110]` (negatives are zero-extended through the unsigned type; the width is a minimum).. Conflicts with binary.hpp's `bin` (see above).
### `bytes(uint64_t)` → `bytes_view`
- IEC units B/KiB/MiB/GiB/TiB/PiB, 1 truncated decimal (omitted if 0): `0 B`, `1023 B`, `1 KiB`, `1.5 KiB`, `1 MiB`, `5 GiB`, `1 PiB`, `1024 PiB`(1<<60), `bytes(1100)` `[1 KiB]` (0.07 truncated), `bytes(UINT64_MAX)` `[16383.9 PiB]`.
### `addr_offset(uintptr_t addr, uintptr_t base)` → `0xBASE±0xOFFSET` : `[0x1000+0x10]`, `addr_offset(0x1000,0x1010)` `[0x1010-0x10]`, equal `[0x1000+0x0]` (lowercase, unpadded).
### `mem_range(uintptr_t start, uintptr_t end)` → `[0x1000..0x2000] (4 KiB)`, `[0x1000..0x1100] (256 B)`, end<start `[0x2000..0x1000] (invalid)`.
### `align(string_view text, size_t width, align_mode mode=left, char pad_char=' ')` → `aligned_text_view` (`enum class align_mode{left,right,center}`)
- `align("ab",6)` `[ab    ]`, `right` `[    ab]`, `align("ab",7,center,'*')` `[**ab***]` (extra pad on right), text longer than width → unchanged (no truncation). Borrows text.
### `join(span of const T, string_view delim)` → `joined_span_view` (borrows; default delimiter is a comma and a space)
- Needs an explicit `span` of const T (T not deducible from arrays); elements via `element_formatter` with EMPTY spec. `join(span<const int>(arr,size_t{3}))` `[1, 2, 3]`, a dash as the delimiter gives `[1-2-3]`, empty `[]`, doubles `[1.5, 2.5]` (needs floating.hpp). Spec on the view is ignored (`{:x}` → `[1, 2, 3]`). For ranges/iterators use `ranges.hpp`'s `join`.

## `escaped.hpp` — C-style escaped string literal
- **Include:** `<microfmt/formatters/escaped.hpp>`
- **Factories → `escaped_view{span<const char> data; bool quote=true; bool escape_quotes=true}`** (borrows):
  - `escaped(string_view sv, bool quote=true, bool escape_quotes=true)`
  - `escaped(const char* str, size_t len, bool quote=true, bool escape_quotes=true)`
  - `escaped(span<const uint8_t> bytes, bool quote=true, bool escape_quotes=true)`
  - `escaped(const char (&arr)[N], quote, escape_quotes)` — drops ONE trailing `\0`, **keeps embedded `\0`** (so literals with `\0` in the middle render fully, unlike the string_view overload)
  - `escaped(const uint8_t (&arr)[N], ...)` — all N bytes
- **Spec:** none (ignored).
- Escapes (same table as `{:?}` on strings): `\0 \a \b \t \n \v \f \r \\`, `"` as backslash-quote when `escape_quotes`, printable 0x20–0x7e verbatim, everything else (incl. UTF‑8) `\xHH` lowercase. `'` never escaped.

```text
escaped("line 1\nline 2") ["line 1\nline 2"]   escaped("q\"uote'") ["q\"uote'"]   escaped("q\"uote'",false,false) [q"uote']   escaped("q\"uote",true,false) ["q"uote"]
escaped("a\tb\\c\x01\x7f\xc3\xa9\a\b\v\f\r\0z") ["a\tb\\c\x01\x7f\xc3\xa9\a\b\v\f\r\0z"]   escaped("abc",2) ["ab"]   escaped(uint8_t[]{'M','a','n'}) ["Man"]
```

## `hash.hpp` — `std::hash` value
- **Include:** `<microfmt/formatters/hash.hpp>`; `as_hash(const T&)` → `hash_view<T>` (needs `std::hash<T>`; rvalue deleted; borrows). `.compute_hash()` returns `size_t`.
- **Spec:** forwarded in full to `formatter<size_t>` = integer grammar `[#][0][width][x|X]`. `{:016X}` → `[0000000000000005]` for `std::hash<int>(5)` (libstdc++ identity); `{:#x}` `[0x5]`.

## `errno.hpp` — POSIX errno text
- **Include:** `<microfmt/formatters/errno.hpp>`; `format_errno(int)` / `current_errno()` (reads `errno` now) → `posix_errno{int value}`. **Spec:** none.
- Output `<strerror text> (os:<N>)`: `[No such file or directory (os:2)]`, `[Success (os:0)]`, `[Unknown error 99999 (os:99999)]`. (glibc wording; uses `strerror_r`, 256-byte stack buf; `MICROFMT_USE_SYSTEM_ERROR=1` uses `std::error_code::message()` which allocates; `RELOCO_KERNEL` expects user‑provided `write_errno_string`.)

## `error.hpp` — `std::error_code` / `std::error_category`
- **Include:** `<microfmt/formatters/error.hpp>`. **Spec:** none.
- `error_code` → `<message> [<category>:<value>]`: `[No such file or directory [generic:2]]`, `error_code{}` → `[Success [system:0]]`. `error_category` → its `name()`: `[generic]`, `[system]` (`"unknown_category"` if null). Allocates (`ec.message()` is std::string).

## `exception.hpp` — `std::exception` family
- **Include:** `<microfmt/formatters/exception.hpp>`. **Spec:** none.
- `formatter<T>` for any `T` derived from `std::exception` → `what()` (null → `<unknown exception>`): `runtime_error("boom")` `[boom]`, `std::bad_alloc{}` `[std::bad_alloc]`, `out_of_range("oor")` `[oor]`.
- `std::system_error` → `<what> [<category>:<value>]`: `system_error(ec,"ctx")` `[ctx: No such file or directory [generic:2]]`. (Full specialization wins over the base‑of rule.) Works through a `const std::exception&` too (virtual `what()`).

## `ansi.hpp` — ANSI SGR styling (namespace `microfmt::ansi`)
- **Include:** `<microfmt/formatters/ansi.hpp>`. **Spec:** none for `style` and `styled_view<T>` (inner value formatted with an EMPTY spec).
- **Types:** `enum class color : uint8_t {none, black=30 … white=37, bright_black=90 … bright_white=97}`; `enum class attribute : uint8_t {none, bold=1, dim=2, italic=4, underline=8, blink=16, reverse=32}` with `operator|` and bool‑returning `operator&`; `struct style{color fg=none; color bg=none; attribute attr=none; static bool colors_enabled /*default true*/;}`.
- **Presets (`inline constexpr style`):** `reset`, `fg_{black,red,green,yellow,blue,magenta,cyan,white,gray}`, `fg_bright_{black,red,green,yellow,blue,magenta,cyan,white}`, `bg_*` same set, `bold dim italic underline blink reverse`, `error_style`(bright red+bold), `warn_style`(bright yellow+bold), `ok_style`(bright green+bold), `info_style`(bright cyan). Combine manually: `style{color::green, color::black, attribute::bold | attribute::underline}`.
- **Wrappers:** `styled(const T& v, style st)`, `red/green/yellow/blue/magenta/cyan/white/gray(const T&)` → `styled_view<T>{const T& value; style st}` — **stores a reference** (don't keep across statements; temporaries live until end of the full expression).
- **Output:** `ESC[<codes>m` with codes in order: attrs (1,2,3,4,5,7), fg, bg(+10), `;`-joined; empty style → `ESC[0m`; `styled_view` = style + value + `ESC[0m`. `colors_enabled=false` suppresses ALL escapes (style prints nothing, styled prints plain value).

```text
red("failure")                       "\x1b[31mfailure\x1b[0m"
ansi::bold                           "\x1b[1m"           ansi::reset / style{}  "\x1b[0m"
styled(42, style{color::green,color::black,attribute::bold|attribute::underline})  "\x1b[1;4;32;40m42\x1b[0m"
fg_bright_cyan "\x1b[96m"  bg_red "\x1b[41m"  error_style "\x1b[1;91m"  styled("x",warn_style) "\x1b[1;93mx\x1b[0m"  gray(1.5) "\x1b[90m1.5\x1b[0m"
style{dim|italic|blink|reverse} "\x1b[2;3;5;7m"   styled(255,bold) with {:x} → "\x1b[1m255\x1b[0m" (spec ignored)
ansi::style::colors_enabled=false; red("plain") → "plain"; reset → ""
```
- **Notes:** `colors_enabled` is a plain global (not thread-safe). Names like `ansi::bold`, `ansi::reset`, `ansi::red` are common identifiers — don't `using namespace microfmt::ansi` carelessly. `emit_style(style, sink)` / `emit_reset(sink)` are public.

## `fmt.hpp` — `{fmt}`-compatible shim (namespace `fmt`) + bridge
- **Include:** `<microfmt/formatters/fmt.hpp>` (pulls `ranges.hpp`). Not the real {fmt}; defines namespace `fmt` (conflicts with real {fmt} in the same TU).
- **API:**

| fmt:: | Behaviour |
|---|---|
| `format<Capacity>(fmt, args...)` → `buffer_sink<Capacity>` | |
| `format(fmt, args...)` → `buffer_sink<128>` (`DEFAULT_FORMAT_BUFFER_SIZE`) | truncates at 128 |
| `format_to(char* out, fmt, args...)` → `char*` | unbounded write |
| `format_to_n(char* out, size_t n, fmt, args...)` → `format_to_n_result<char*>{out,size}` | `size` = total untruncated length: `("abcdefgh{}",n=4)` → `abcd`, size=9, out-buf=4; no NUL written |
| `print(fmt,...)`, `print(FILE*,fmt,...)`, `println(...)`, `println(FILE*,...)` | stdout/FILE |
| `join(range, delim)`, `join(first,last,delim)` | → `microfmt::join` (ranges.hpp) |
| `string_view` alias, `format_parse_context{spec; begin() end() advance_to()}`, `format_context{out()}`, `sink_output_iterator`, `template<T,Char,Enable> formatter` (primary declared only) | for writing bridge formatters |

- **Bridge:** any `T` with `fmt::formatter<T>` providing `parse(fmt::format_parse_context&)` and `format(const T&, fmt::format_context&)` becomes a `microfmt::formatter<T>`; spec is passed through unchanged.

```cpp
struct pt{int x,y;};
template<> struct fmt::formatter<pt>{ constexpr void parse(fmt::format_parse_context&) noexcept{}
  void format(const pt& p, fmt::format_context& c) const noexcept { microfmt::format_to(c.out(), "<{},{}>", p.x, p.y);} };
ns::format<..>("{}", pt{1,2})            // -> [<1,2>]
fmt::format<64>("value={:04X}", 255)      // -> [value=00FF]
fmt::join(std::vector<int>{1,2,3}, "; ") // -> [1; 2; 3]
```
- **Note:** `microfmt::format_to(ctx.out(), ...)` works because `sink_output_iterator` is an output iterator (char‑by‑char `put`); `format_context::out()` returns the iterator, and `fmt::format_to(char*)` is a separate pointer-only overload.

---------------------------------------------------------------------------
## Quick index

| Want | Use | Spec |
|---|---|---|
| decimal / hex int, width, 0-pad | plain int | `[#][0][width≤65535][bxX]` (neg hex = `-ff`) |
| bit pattern of negative in hex | `hex(v)` / cast unsigned | none |
| binary text | `bin` with a Bits template argument (binary.hpp) / `bin(v,w,prefix)` (format_helpers) / `bin_grouped(v,g,sep)` (base_views) | binary.hpp: `#` `_` |
| char / string / `const char*` | plain | `[?]` only |
| padded/aligned/truncated string | `as_string(sv)` | `[[fill]align][width][.prec][?]` |
| styled text (pad, case, quote, trunc) | `pad`/`pad_center`/`pad_right`/`to_upper`/`to_lower`/`microfmt::truncate`/`quoted` | `[[fill]align][width][u l t q b .N]` |
| std::string | `string.hpp` | none |
| float/double | `floating.hpp` | `[+ -][#][0][width][.N][fFeEgGaA]` |
| float debug (`1.0`) | `as_debug(lvalue)` + `debug_float.hpp` | none |
| complex | `std_complex.hpp` | forwarded to float |
| path / bitset / byte / ratio | `std_types.hpp` | path none; bitset `#`; byte int-spec; ratio none |
| Debug of array/bitset/ref_wrapper/byte | `std_debug.hpp` + `as_debug` | none |
| `1,234,567` | `thousands(v, sep, group)` | none |
| fixed-point `3.300` | `milli/centi/micro/fixed(raw)` (Scale and Decimals template arguments) | none |
| base64 | `base64(span of const uint8_t)` | none |
| escaped C literal | `escaped(...)` | none |
| hash value | `as_hash(v)` | integer spec (`x`, `016X`…) |
| errno text | `format_errno(e)` / `current_errno()` | none |
| error_code / category | `error.hpp` | none |
| exceptions | `exception.hpp` | none |
| colors | `ansi::red(v)`, `ansi::styled(v, style)`, style presets | none |
| size `1.5 KiB`, `0x..+0x..`, `[a..b] (n)`, `align`, `join(span)` | `bytes`, `addr_offset`, `mem_range`, `align`, `join` | none |
| raw address | `(void*)p` / `T*` | `[0]width` hex digits |
| force Debug | `as_debug(lvalue)` | forwarded to `formatter<T>` if no `Debug<T>` |
| `{fmt}` shim / bridge | `fmt.hpp` | passthrough |
| output | `format` with a size template argument, `format_to`, `format_as` with a container template argument, `print/println`, `make_format_args`+`vformat_args_to`, `MICROFMT_STRING` | — |
