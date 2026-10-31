<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Cheatsheet: ranges, structures, Boost, and reloco

Part of the [formatter cheatsheet](../cheatsheet.md). All outputs below were produced by compiling and running real code (g++ 15, `-std=c++20`, Boost present,
reloco from `/home/jarek/Projekty/reloco/include`) unless marked **(output unverified)**.
Format-string reminders: `{}` auto index, `{N}`/`{N:spec}` zero-based positional, `{{ }}` escapes, no named/dynamic width.

## Common rules for this group

- **Element-spec forwarding:** the spec (or the remainder after a delimiter flag) is handed to the element formatter *as a fresh spec per element* — e.g. `{:04X}` → every int `%04X`, `{:?}` → every string quoted/escaped.
- **Two lookup strategies:**
  - `join`, `join_as`, `enumerate`, `zip`, `filter`, `map_view`, tuple, and *all reloco containers/wrappers* use `detail::element_formatter<T>` → tries `formatter<T>`, then `reloco::Display<T>`, then `reloco::Debug<T>`.
  - `repeat`, `vec/vec3/mat`, `std::optional`/`reloco::optional`/`expected`/`checked_value`, and every `boost_*` formatter use `formatter<T>` **directly**: elements with only `Display<T>`/`Debug<T>` do not compile there.
- **Delimiter-flag matrix** (only the *first* char of the spec is inspected; case-insensitive; the rest is forwarded):

| Formatter | `b` | `c` | `n` | `p` | default |
|---|---|---|---|---|---|
| `join`/`join_as`/`enumerate`/`zip`/`repeat`/`vec`/reloco seq | not a flag (forwarded to element, normally ignored) | same | same | same | see each |
| `filter(...)` | `[ ]` | `{ }` | none | **not a flag** | `[ ]` |
| `map_view(...)` | `[ ]` | `{ }` | none | **not a flag** | `{ }` |
| tuple-like (`std::tuple`, `std::pair`, …) | `[ ]` | `{ }` | none | `( )` | `( )` |

  Only **one** flag char is consumed: `{:bn}` on a tuple → `[ ]` and `n` is forwarded to the elements.
- **Debug flag:** `?` (as first char of the spec reaching a string/char-like element) quotes+escapes. Via a delimiter flag use `{:n?}`, `{:b?}`, `{:c?}`.
- **Unknown/unsupported spec chars** are silently ignored by int/string formatters (`{:q}` on ints prints the plain value). The int formatter accepts `[#][0][width][b|x|X]` only.
- **Float spec caveat (floating.hpp, used by `vec`, `fixed_point`):** `.Nf`/`e`, a leading *width* and `0` padding (`{:8.2f}`, `{:08.2f}`) all work; there are no fill/alignment characters.
- **Views borrow.** `join`, `enumerate`, `zip`, `filter`, `map_view` store pointers/references to your range; rvalue ranges are `= delete`d (compile error). `repeat`/`vec3` store by value.
- `math.hpp` and `units.hpp` include `floating.hpp`, so `float`/`double` elements work without extra includes.

---

## `ranges.hpp` — join iterator ranges with a delimiter

### `join(range, delimiter = ", ")` / `join(first, last, delimiter = ", ")`
- **Include:** `<microfmt/formatters/ranges.hpp>`
- **Factories:**
  - `template <class Range> constexpr auto join(const Range &range, string_view delimiter = ", ") noexcept` — any type with `begin()/end()` (via `std::begin/end`); rvalue `Range&&` overload is deleted.
  - `template <class It, class Sentinel> constexpr auto join(It first, Sentinel last, string_view delimiter = ", ") noexcept` — disabled when `Sentinel` converts to `string_view` (so `join(range, "x")` picks the range overload).
  - Result type `join_view<It, Sentinel>{first, last, delimiter}` (`RELOCO_POINTER`; delimiter is a non-owning `string_view`).
- **Spec:** `{:<elem-spec>}` — the *entire* spec goes to every element. No delimiter flags (`b/c/n/p` are NOT consumed; `{:b}` just goes to elements). No brackets are emitted; empty range prints nothing.

| Token | Meaning |
|---|---|
| *(anything)* | forwarded verbatim to each element's formatter (`04X`, `#x`, `?`, `.2f`, …) |

- **Example:**

```cpp
std::vector<int> v{10, 255, 7};
microfmt::format_to(out, "{}", microfmt::join(v));                 // -> 10, 255, 7
microfmt::format_to(out, "{:04X}", microfmt::join(v, "|"));        // -> 000A|00FF|0007
microfmt::format_to(out, "{:#x}", microfmt::join(v.begin(), v.end(), " ")); // -> 0xa 0xff 0x7
std::vector<std::string_view> s{"a", "b\n"};
microfmt::format_to(out, "{:?}", microfmt::join(s));               // -> "a", "b\n"
microfmt::format_to(out, "[{}]", microfmt::join(emptyv));          // -> []
microfmt::format_to(out, "{:b}", microfmt::join(v));               // -> 10, 255, 7   ('b' now prints ints in binary)
std::map<int,int> m{{1,2},{3,4}};
microfmt::format_to(out, "{}", microfmt::join(m));                 // -> (1, 2), (3, 4)  (pairs via tuple formatter)
std::vector<std::tuple<int,int>> vt{{1,2},{3,4}};
microfmt::format_to(out, "{:b}", microfmt::join(vt));              // -> [1, 2], [3, 4]  ('b' forwarded to each tuple!)
```
- **Notes:** the range must outlive the format call. Element type deduced from `*it` (cv/ref stripped). Elements without any formatter → compile error (`incomplete type formatter<T>`); `std::vector` has no formatter of its own, so nested ranges need an explicit inner `join`.

### `join_as<Delim, ElemSpec = "">(range)` / `join_as<Delim, ElemSpec = "">(first, last)` (C++20 only)
- **Include:** `<microfmt/formatters/ranges.hpp>` (only exists when `RELOCO_CXX20`).
- **Factories:** `template <detail::fixed_string Delim, detail::fixed_string ElemSpec = "", class Range> constexpr auto join_as(const Range &)` (rvalue deleted); `template <fixed_string Delim, fixed_string ElemSpec = "", class It, class Sentinel> constexpr auto join_as(It, Sentinel)` (Sentinel not convertible to `string_view`). Result: `join_as_view<It, Sentinel, Delim, ElemSpec>`. Delimiter and element spec are template string-literals (NTTP), no runtime storage.
- **Spec:** `{:<runtime-elem-spec>}` — **used only when the compile-time `ElemSpec` is empty**; if `ElemSpec` is non-empty it wins and the runtime spec is ignored.
- **Example:**

```cpp
microfmt::format_to(out, "{}",    microfmt::join_as<"; ">(v));                 // -> 10; 255; 7
microfmt::format_to(out, "{}",    microfmt::join_as<"-", "04x">(v));           // -> 000a-00ff-0007
microfmt::format_to(out, "{:X}",  microfmt::join_as<"-", "">(v));              // -> A-FF-7      (runtime spec used)
microfmt::format_to(out, "{:X}",  microfmt::join_as<"-", "04x">(v));           // -> 000a-00ff-0007  (compile-time wins)
microfmt::format_to(out, "{:X}",  microfmt::join_as<"-">(v.begin(), v.end())); // -> A-FF-7
```

---

## `sequence_views.hpp` — `enumerate` and `zip`

### `enumerate(range, delimiter = ", ")`
- **Include:** `<microfmt/formatters/sequence_views.hpp>` (also pulls `ranges.hpp`)
- **Factory:** `template <class Range> constexpr auto enumerate(const Range &, string_view delimiter = ", ") noexcept` → `enumerate_view<Range>{const Range*, delimiter}`; rvalue deleted.
- **Spec:** `{:<elem-spec>}` — whole spec forwarded to each *value*; the index (`size_t`, 0-based) is always printed with the default decimal formatter; format is `index: value`. No delimiter flags, no brackets.
- **Example:**

```cpp
microfmt::format_to(out, "{}",   microfmt::enumerate(v));          // -> 0: 10, 1: 255, 2: 7
microfmt::format_to(out, "{:x}", microfmt::enumerate(v, " | "));   // -> 0: a | 1: ff | 2: 7
microfmt::format_to(out, "{:?}", microfmt::enumerate(s));          // -> 0: "a", 1: "b\n"
```

### `zip(a, b, delimiter = ", ")`
- **Factory:** `template <class R1, class R2> constexpr auto zip(const R1 &, const R2 &, string_view delimiter = ", ") noexcept` → `zip_view<R1,R2>`; deleted if *either* argument is an rvalue.
- **Spec:** `{:<elem-spec>}` forwarded to the elements of **both** ranges. Each pair is `(a, b)`; stops at the shorter range.
- **Example:**

```cpp
std::vector<int> v{10,255,7}, w{1,2};
microfmt::format_to(out, "{}",   microfmt::zip(v, w));          // -> (10, 1), (255, 2)
microfmt::format_to(out, "{:x}", microfmt::zip(v, w, "; "));    // -> (a, 1); (ff, 2)
```

---

## `filter_view.hpp` — print only elements matching a predicate

### `filter(range, pred)` / `filter(ptr, count, pred)`
- **Include:** `<microfmt/formatters/filter_view.hpp>`
- **Factories:**
  - `template <class Range, class Pred> constexpr auto filter(const Range &rng, Pred &&pred) noexcept` → `filtered_range_view<Range, decay_t<Pred>>` (holds `const Range &`, predicate by value). Rvalue `Range&&` deleted.
  - `template <class T, class Pred> constexpr auto filter(const T *ptr, size_t count, Pred &&pred) noexcept` → `filtered_pointer_range_view<T, Pred>`; `ptr == nullptr` forces count 0.
  - Struct members `open_delim='[' close_delim=']' separator=", "` exist but no factory parameter sets them (only spec chooses delimiters).
- **Spec:** `{:[b|c|n][elem-spec]}` — first char `b/B`→`[ ]`, `c/C`→`{ }`, `n/N`→no delimiters; **`p` is not recognized** (goes to element). Default `[ ]`. Remainder → each matching element.

| Token | Meaning |
|---|---|
| `b` | `[` `]` (default anyway) |
| `c` | `{` `}` |
| `n` | no delimiters |
| rest | element spec (`04X`, `#x`, `?`, …) |

- **Example:**

```cpp
auto odd = [](int x) { return x % 2 == 1; };      // v = {10, 255, 7}
microfmt::format_to(out, "{}",     microfmt::filter(v, odd));                       // -> [255, 7]
microfmt::format_to(out, "{:c}",   microfmt::filter(v, odd));                       // -> {255, 7}
microfmt::format_to(out, "{:n}",   microfmt::filter(v, odd));                       // -> 255, 7
microfmt::format_to(out, "{:b04X}",microfmt::filter(v, [](int x){return x>8;}));    // -> [000A, 00FF]
microfmt::format_to(out, "{:N#x}", microfmt::filter(v, [](int x){return x>8;}));    // -> 0xa, 0xff
microfmt::format_to(out, "{:X}",   microfmt::filter(v.data(), v.size(), [](int x){return x>8;})); // -> [A, FF]
microfmt::format_to(out, "{}",     microfmt::filter(static_cast<const int*>(nullptr), 5, odd));   // -> []
microfmt::format_to(out, "{:q}",   microfmt::filter(v, odd));                       // -> [255, 7]  (unknown elem spec ignored)
```
- **Notes:** predicate is evaluated lazily at format time; separator is fixed `", "`.

---

## `repeated_view.hpp` — one value N times

### `repeat(value, count)` / `repeat(value, count, separator)`
- **Include:** `<microfmt/formatters/repeated_view.hpp>`
- **Factories:** `template <class T> constexpr auto repeat(T value, size_t count) noexcept` (separator `""`); `template <class T> constexpr auto repeat(T value, size_t count, string_view separator) noexcept` → `repeated_view<decay_t<T>>`.
- **Spec:** `{:<spec>}` — the **entire** spec is forwarded to `formatter<T>` and re-applied on each repetition (no delimiter flags).
- **Example:**

```cpp
microfmt::format_to(out, "{}",     microfmt::repeat('*', 5));                    // -> *****
microfmt::format_to(out, "{:04x}", microfmt::repeat(0x2A, 3, "|"));              // -> 002a|002a|002a
microfmt::format_to(out, "{}",     microfmt::repeat("ab", 3, ", "));             // -> ab, ab, ab
microfmt::format_to(out, "[{}]",   microfmt::repeat(1, 0, ","));                 // -> []
microfmt::format_to(out, "{:?}",   microfmt::repeat(std::string_view("x"), 2, "+")); // -> "x"+"x"
```
- **Notes:** value is stored by decayed copy — a string literal decays to `const char*` (pointer must outlive the format call); separator `string_view` is borrowed. Uses `formatter<T>` directly (no Display/Debug fallback).

---

## `map_view.hpp` — `key: value` entries

### `map_view(container)` / `map_view(container, key_fn, val_fn)` / `map_view(first, last, key_fn = default_key_fn{}, val_fn = default_val_fn{})`
- **Include:** `<microfmt/formatters/map_view.hpp>`
- **Factories:**
  - `template <class M> constexpr auto map_view(const M &m) noexcept` (uses `m.begin()/m.end()`; rvalue deleted).
  - `template <class M, class KeyFn, class ValFn> constexpr auto map_view(const M &m, KeyFn&&, ValFn&&) noexcept` (rvalue deleted).
  - `template <class It, class Sentinel, class KeyFn = default_key_fn, class ValFn = default_val_fn> constexpr auto map_view(It first, Sentinel last, KeyFn&& = {}, ValFn&& = {}) noexcept` — enabled only if `*first` is valid.
  - Extractor callables take the *element* (`*it`) and return key / value. `default_key_fn`: `.first`, else `.key()`, else the element itself. `default_val_fn`: `.second`, else `.value()`, else the element itself.
  - Result: `map_range_view<KeyFn, ValFn, It, Sentinel>`; members `open_delim="{" close_delim="}" sep=", " kv_sep=": "` are fixed (no factory param).
- **Spec:** `{:[b|c|n][elem-spec]}` — `b/B`→`[ ]`, `c/C`→`{ }`, `n/N`→none; default `{ }`. **No `p`.** Remainder forwarded to **both keys and values**.

| Token | Meaning |
|---|---|
| `b` | `[k: v, …]` |
| `c` | `{k: v, …}` (default) |
| `n` | `k: v, …` |
| rest | element spec applied to every key *and* value |

- **Example:**

```cpp
std::map<std::string_view,int> m{{"a",1},{"b",255}};
microfmt::format_to(out, "{}",    microfmt::map_view(m));            // -> {a: 1, b: 255}
microfmt::format_to(out, "{:b}",  microfmt::map_view(m));            // -> [a: 1, b: 255]
microfmt::format_to(out, "{:n}",  microfmt::map_view(m));            // -> a: 1, b: 255
microfmt::format_to(out, "{:cx}", microfmt::map_view(m));            // -> {a: 1, b: ff}
microfmt::format_to(out, "{:?}",  microfmt::map_view(m));            // -> {"a": 1, "b": 255}
microfmt::format_to(out, "{:n?}", microfmt::map_view(m));            // -> "a": 1, "b": 255
microfmt::format_to(out, "{}", microfmt::map_view(m,
    [](const auto &e) -> const int &{ return e.second; },
    [](const auto &e) -> const std::string_view &{ return e.first; })); // -> {1: a, 255: b}
microfmt::format_to(out, "{}", microfmt::map_view(m.begin(), m.end())); // -> {a: 1, b: 255}
microfmt::format_to(out, "{}", microfmt::map_view(empty.begin(), empty.end())); // -> {}
```

---

## `tuple.hpp` — `std::tuple`, `std::pair`, any tuple-like

### `formatter<T>` for every `T` with `std::tuple_size` and **no** `.data()` member
- **Include:** `<microfmt/formatters/tuple.hpp>`
- **Types:** `std::tuple<...>`, `std::pair<A,B>`, `std::tuple<>`, user types specializing `std::tuple_size`/`std::get`. `std::array` (has `.data()`), spans, strings are excluded on purpose.
- **Spec:** `{:[b|c|n|p][elem-spec]}` (first char, case-insens.). Default `( )`; separator fixed `", "`; remainder forwarded to **every** element (nested tuples receive it too).

| Token | Meaning |
|---|---|
| `b` | `[a, b]` |
| `c` | `{a, b}` |
| `n` | `a, b` (no delimiters) |
| `p` | `(a, b)` (same as default) |
| rest | element spec |

- **Example:**

```cpp
microfmt::format_to(out, "{}",      std::tuple{1, "x", 2.5});               // -> (1, x, 2.5)   (needs floating.hpp for 2.5)
microfmt::format_to(out, "{:b}",    std::tuple{1, 2});                      // -> [1, 2]
microfmt::format_to(out, "{:c04X}", std::make_tuple(10, 255));              // -> {000A, 00FF}
microfmt::format_to(out, "{:n}",    std::pair{1, 2});                       // -> 1, 2
microfmt::format_to(out, "{:p}",    std::tuple{1, 2});                      // -> (1, 2)
microfmt::format_to(out, "{}",      std::tuple<>{});                        // -> ()
microfmt::format_to(out, "{:b?}",   std::tuple{std::string_view("q"), 'c'});// -> ["q", 'c']
microfmt::format_to(out, "{:x}",    std::pair{255, 16});                    // -> (ff, 10)
microfmt::format_to(out, "{:bn}",   std::pair{1, 2});                       // -> [1, 2]   (only 'b' consumed)
microfmt::format_to(out, "{:x}",    std::tuple{std::pair{255,16}, 3});      // -> ((ff, 10), 3)
```

---

## `math.hpp` — vector / matrix views

### `vec(const T (&arr)[N])`, `vec3(x, y, z)`, `mat<T, Rows, Cols>(const T (&arr)[Rows*Cols])`
- **Include:** `<microfmt/formatters/math.hpp>` (pulls `floating.hpp`).
- **Factories:**
  - `template <class T, size_t N> constexpr auto vec(const T (&)[N]) noexcept` → `vec_view<T,N>` (borrows the array via `span`).
  - `template <class T> constexpr auto vec3(const T &x, const T &y, const T &z) noexcept` → `vec3_view<T>` (stores copies).
  - `template <class T, size_t Rows, size_t Cols> constexpr auto mat(const T (&)[Rows*Cols]) noexcept` → `matrix_view<T,Rows,Cols>` (row-major, borrows; Rows/Cols must be given explicitly).
- **Spec:** `{:<elem-spec>}` — whole spec to each element via `formatter<T>`. No delimiter flags.
- **Output shape:** vector `<a, b, c>`; matrix: `[\n  [r0c0, r0c1]\n  [r1c0, r1c1]\n]` (multi-line, `\n` after every row and after the opening `[`; no trailing newline).
- **Example:**

```cpp
float arr[3]{1.5f, 2, 3};  int ia[4]{1, 2, 3, 4};
microfmt::format_to(out, "{}",    microfmt::vec(arr));            // -> <1.5, 2, 3>
microfmt::format_to(out, "{:.2f}",microfmt::vec(arr));            // -> <1.50, 2.00, 3.00>
microfmt::format_to(out, "{}",    microfmt::vec3(1, 2, 3));       // -> <1, 2, 3>
microfmt::format_to(out, "{:.1f}",microfmt::vec3(1.0, 2.5, 3.25));// -> <1.0, 2.5, 3.2>
microfmt::format_to(out, "{}",    microfmt::mat<int,2,2>(ia));
// -> [
// ->   [1, 2]
// ->   [3, 4]
// -> ]
microfmt::format_to(out, "{:03}", microfmt::mat<int,2,2>(ia));    // -> rows "[001, 002]" / "[003, 004]"
```

---

## `monad.hpp` — optional / expected / checked_value

### `std::optional<T>`, `reloco::optional<T>`
- **Include:** `<microfmt/formatters/monad.hpp>`
- **Spec:** `{:<inner-spec>}` — the whole spec goes to `formatter<T>`. Empty → `None`; otherwise `Some(<value>)`.
- **Example:**

```cpp
microfmt::format_to(out, "{}",     std::optional<int>{5});                  // -> Some(5)
microfmt::format_to(out, "{}",     std::optional<int>{});                   // -> None
microfmt::format_to(out, "{:04X}", std::optional<uint16_t>{0x2a});          // -> Some(002A)
microfmt::format_to(out, "{:?}",   std::optional<std::string_view>{"s"});   // -> Some("s")
microfmt::format_to(out, "{}",     reloco::optional<int>{7});               // -> Some(7)
```

### `microfmt::expected<T,E>` (= `reloco::expected`), `std::expected<T,E>` (C++23, `MICROFMT_HAS_STD_EXPECTED`)
- **Spec:** `{:<spec>}` forwarded to the active alternative (value *or* error). Output `Ok(<v>)` / `Err(<e>)`; `expected<void,E>` ok-case → `Ok()`.
- **Example:**

```cpp
microfmt::format_to(out, "{}",     microfmt::expected<int,int>{3});                        // -> Ok(3)
microfmt::format_to(out, "{:04X}", microfmt::expected<uint16_t,int>{0x2a});                // -> Ok(002A)
microfmt::format_to(out, "{}",     microfmt::expected<int,int>{reloco::unexpected(9)});    // -> Err(9)
microfmt::format_to(out, "{}",     microfmt::expected<void,int>{});                        // -> Ok()
microfmt::format_to(out, "{}",     microfmt::expected<void,int>{reloco::unexpected(4)});   // -> Err(4)
// C++23: std::expected<int,int>{3} -> Ok(3); std::unexpected(4) -> Err(4); std::expected<void,int>{} -> Ok()
```

### `microfmt::checked_value<T>` (= `reloco::checked_value`)
- **Spec:** `{:<spec>}` forwarded to `formatter<T>`; no wrapper text.
- **Example:**

```cpp
checked_value<int> cv{5};
microfmt::format_to(out, "{:03}", cv);      // -> 005
auto cv2 = std::move(cv);
microfmt::format_to(out, "{}", cv);         // -> <moved-from>
```
- **Notes:** `std::optional` formatter is an opt-in interop header; inner type needs a `formatter<T>` (no Display/Debug fallback).

---

## `reflection.hpp` + `reflect_annotate.hpp` — native P2996 reflection (`-std=c++26 -freflection`)

> All outputs in this section are **(output unverified)**: the check toolchain (g++ 15) has no P2996. Derived from source. Without `RELOCO_HAS_REFLECTION`, `reflection.hpp` is an empty header (no error) and `MICROFMT_REFLECT_FORMAT(T)` expands to **nothing**.

### Reflected enums (automatic) — `formatter<E>` for **every** `std::is_enum` type
- **Include:** `<microfmt/formatters/reflection.hpp>`
- **Spec:** none accepted (`parse` ignores it). Prints the enumerator identifier; for an unmapped value prints the underlying integer in plain decimal.
- **Example:** `format_to(out, "{}", color::green)` -> `green`; `format_to(out, "{}", static_cast<color>(99))` -> `99`. (unverified)
- **Notes:** enabled for all enums, except enums that `boost_describe.hpp` already formats (it sets `detail::enum_has_dedicated_formatter<E>`), so including both headers is safe. Enumerators with duplicate values print the first match.

### Reflected structs — `MICROFMT_REFLECT_FORMAT(Type)` + `enable_reflect_format<Type>`
- **Include:** annotation: `<microfmt/formatters/reflect_annotate.hpp>` (always safe, any compiler); live formatter: `<microfmt/formatters/reflection.hpp>`.
- **Macros / customization:**
  - `template <class T> struct enable_reflect_format : std::false_type {}` (primary).
  - `MICROFMT_REFLECT_FORMAT(Type)` → `template <> struct microfmt::enable_reflect_format<Type> : std::true_type {}; MICROFMT_REFLECT_DUMP_STRUCT(Type)`; place at global (or `microfmt`) namespace scope, with `Type` fully qualified. `MICROFMT_REFLECT_DUMP_ENUM(Type)` / `MICROFMT_REFLECT_DUMP_STRUCT(Type)` are no-ops unless built with `-DMICROFMT_REFLECT_DUMP_MODE` (code generator in `tools/reflect_dump/`).
- **Spec:** none accepted. Output: `{name: value, name: value}` for public non-static data members in declaration order; each member via `format_to(out, "{}", member)` (so nested reflected structs recurse; member needs a formatter). Fieldless struct → `{}`.
- **Example:**

```cpp
struct point { int x; int y; };
MICROFMT_REFLECT_FORMAT(point);
microfmt::format_to(out, "{}", point{3, 4});   // -> {x: 3, y: 4}   (unverified)
```
- **Notes:** generator output differs from live output: the *generated* header specializes `reloco::Debug<T>` printing `TypeName { x: 3, y: 4 }` (fields via `as_debug`; `TypeName {}` for empty) and `formatter<E>` printing names / underlying int; wrapped in `#if !RELOCO_HAS_REFLECTION`. Live formatter is `{x: 3, y: 4}` with `{}`-formatting of fields and no type name.

---

## `boost_describe.hpp` — Boost.Describe structs & enums

### Described enums (`BOOST_DESCRIBE_ENUM`) and structs (`BOOST_DESCRIBE_STRUCT`/`CLASS`)
- **Include:** `<microfmt/formatters/boost_describe.hpp>` (needs Boost.Describe + MP11).
- **Spec:** none accepted for either (spec ignored).
  - enum: enumerator name; unmapped value → text `static_cast<uint8_t>(N)` (1-byte underlying), `static_cast<uint16_t>(N)` (2-byte), `static_cast<uint32_t>(N)` (4-byte), else `static_cast<uint64_t>(N)` (8-byte).
  - struct: `{name: value, ...}` over `mod_public` members (`describe_members<T, mod_public>`), values via `format_to(out, "{}", member)`; empty struct → `{}`.
- **Example:**

```cpp
enum class color { red, green, blue };  BOOST_DESCRIBE_ENUM(color, red, green, blue)
enum class small : unsigned char { a = 1, b = 2 };  BOOST_DESCRIBE_ENUM(small, a, b)
struct point { int x; int y; };  BOOST_DESCRIBE_STRUCT(point, (), (x, y))
struct line { point a; const char *name; color c; double w; };  BOOST_DESCRIBE_STRUCT(line, (), (a, name, c, w))
format_to(out, "{}", color::green);                 // -> green
format_to(out, "{}", static_cast<color>(99));       // -> static_cast<uint32_t>(99)  (uint8_t/16/64 for other underlying sizes)
format_to(out, "{}", static_cast<small>(77));       // -> static_cast<uint8_t>(77)
format_to(out, "{}", point{3, 4});                  // -> {x: 3, y: 4}
format_to(out, "{:x}", point{255, 4});              // -> {x: 255, y: 4}   (spec ignored)
format_to(out, "{}", line{{1,2}, "ln", color::blue, 1.5}); // -> {a: {x: 1, y: 2}, name: ln, c: blue, w: 1.5}
```
- **Notes:** the struct formatter calls `microfmt::format_to` qualified, so it stays unambiguous with `std::format_to` when `<format>`/`<chrono>` are included and members are `string_view`s.

---

## `boost_containers.hpp` — Boost.Container sequences

### `boost::container::static_vector<T,Cap,Opt>`, `small_vector<T,N,Alloc,Opt>`
- **Include:** `<microfmt/formatters/boost_containers.hpp>` (pulls `ranges.hpp`)
- **Spec:** `{:<elem-spec>}` forwarded to each element (via `join`); output `[a, b, c]` with `", "`. **No delimiter flags.**
- **Example:**

```cpp
boost::container::static_vector<int,4> sv{1,255};
boost::container::small_vector<int,2> sm{1,2,3};
format_to(out, "{}", sv);                  // -> [1, 255]
format_to(out, "{:04X}", sm);              // -> [0001, 0002, 0003]
format_to(out, "{}", boost::container::static_vector<int,4>{});            // -> []
format_to(out, "{:?}", boost::container::static_vector<std::string_view,4>{"a"}); // -> ["a"]
```

---

## `boost_monad.hpp` — boost::optional, variant2, Outcome

### `boost::optional<T>`
- **Spec:** `{:<inner-spec>}` → `Some(<v>)` / `None`. Uses `formatter<T>` directly.

```cpp
format_to(out, "{}", boost::optional<int>{4});        // -> Some(4)
format_to(out, "{}", boost::optional<int>{});         // -> None
format_to(out, "{:04x}", boost::optional<int>{255});  // -> Some(00ff)
```

### `boost::variant2::variant<Ts...>`
- **Spec:** `{:[i|#][alt-spec]}` — if the **first** char is `i` or `#`, wrap output as `variant[<index>](<value>)`; the remainder goes to the active alternative's `formatter<T>`.

| Token | Meaning |
|---|---|
| `i` or `#` (first char only) | print `variant[N](...)` wrapper (N = active index) |
| rest | spec for the active alternative |

```cpp
boost::variant2::variant<int, std::string_view> var{std::string_view("hi")};
format_to(out, "{}", var);                                  // -> hi
format_to(out, "{:i}", var);                                // -> variant[1](hi)
format_to(out, "{:#}", boost::variant2::variant<int,std::string_view>{255});    // -> variant[0](255)
format_to(out, "{:i04x}", boost::variant2::variant<int,std::string_view>{255}); // -> variant[0](00ff)
format_to(out, "{:x}", boost::variant2::variant<int,std::string_view>{255});    // -> ff
format_to(out, "{:#?}", var);                               // -> variant[1]("hi")
```
- **Notes:** `{:?}` alone (no `i`/`#`) just forwards `?`. Every alternative needs a `formatter<T>` (visit instantiates all).

### `boost::outcome_v2::basic_result<R,S,Policy>` (`result<R,S>`)
- **Spec:** `{:<spec>}` forwarded to value (`Ok(...)`) or error (`Err(...)`); `R=void` → `Ok()`; neither value nor error → `NoValue`.

```cpp
outcome_v2::result<int, std::string_view> ok{3}, er{outcome_v2::failure(std::string_view("bad"))};
format_to(out, "{}", ok);          // -> Ok(3)
format_to(out, "{}", er);          // -> Err(bad)
format_to(out, "{:03}", ok);       // -> Ok(003)
format_to(out, "{}", outcome_v2::result<void,int>{outcome_v2::success()}); // -> Ok()
```
- **Notes:** `result<int,int>` is ambiguous in Outcome itself (can't construct from an int) — unrelated to microfmt.

---

## `boost_net.hpp` — Boost.Asio IP

### `ip::address_v4`, `ip::address_v6`, `ip::address`, `ip::basic_endpoint<P>` (tcp/udp endpoints)
- **Include:** `<microfmt/formatters/boost_net.hpp>`; **Spec:** none accepted (ignored).
- **Shapes:** v4 `a.b.c.d`; v6 canonical RFC-5952-like lowercase hex, longest run (≥2) of zero groups compressed to `::` (first on ties), `%scope_id` appended if non-zero; `address` dispatches v4/v6; endpoint v4 `a.b.c.d:port`, v6 `[addr]:port`.

```cpp
format_to(out, "{}", make_address_v4("192.168.1.10"));    // -> 192.168.1.10
format_to(out, "{}", make_address_v6("2001:db8::1"));     // -> 2001:db8::1
format_to(out, "{}", make_address_v6("::"));              // -> ::
format_to(out, "{}", make_address_v6("1:0:0:2:0:0:0:3")); // -> 1:0:0:2::3
format_to(out, "{}", make_address_v6("fe80::1%3"));       // -> fe80::1%3
format_to(out, "{}", make_address_v6("1:2:3:4:5:6:7:8")); // -> 1:2:3:4:5:6:7:8
format_to(out, "{}", address(make_address_v4("10.0.0.1")));// -> 10.0.0.1
format_to(out, "{}", address());                          // -> 0.0.0.0
format_to(out, "{}", tcp::endpoint(make_address("10.0.0.1"), 8080));   // -> 10.0.0.1:8080
format_to(out, "{}", udp::endpoint(make_address("2001:db8::1"), 53));  // -> [2001:db8::1]:53
```
- **Notes:** no IPv4-mapped (`::ffff:a.b.c.d`) special casing; no allocation.

---

## `boost_system.hpp` — Boost.System errors

### `boost::system::error_code`, `boost::system::error_condition`
- **Spec:** none. Output `<category name>:<value>` (numeric value, **no message text**).

```cpp
format_to(out, "{}", boost::system::errc::make_error_code(boost::system::errc::invalid_argument)); // -> generic:22
format_to(out, "{}", boost::system::errc::make_error_condition(boost::system::errc::no_such_file_or_directory)); // -> generic:2
format_to(out, "{}", boost::system::error_code());        // -> system:0
```

---

## `boost_time.hpp` — Boost.Chrono & Boost.DateTime

### `boost::chrono::duration<Rep,Period>`
- **Spec:** any `c`/`C` **anywhere** in the spec hides the unit suffix; all other chars ignored (count is formatted with an empty spec: no `x`, no width).
- **Suffix:** `ns` `us` `ms` `s` `min` `h` for periods 1/1e9, 1/1e6, 1/1e3, 1, 60, 3600; otherwise literal ` [custom]` (with leading space).

```cpp
format_to(out, "{}",  boost::chrono::milliseconds(250));  // -> 250ms
format_to(out, "{:c}",boost::chrono::seconds(3));         // -> 3
format_to(out, "{}",  boost::chrono::minutes(2));         // -> 2min
format_to(out, "{}",  boost::chrono::hours(1));           // -> 1h
format_to(out, "{}",  boost::chrono::nanoseconds(5));     // -> 5ns
format_to(out, "{}",  boost::chrono::microseconds(5));    // -> 5us
format_to(out, "{}",  boost::chrono::duration<int, boost::ratio<1,100>>(5)); // -> 5 [custom]
format_to(out, "{}",  boost::chrono::duration<double, boost::ratio<1>>(1.5));// -> 1.5s  (needs floating.hpp)
format_to(out, "{:x}",boost::chrono::milliseconds(255));  // -> 255ms
```

### `boost::chrono::time_point<Clock,Duration>`
- **Spec:** passed to the duration formatter (so `c` hides the unit). Output `<duration> since epoch`.

```cpp
using tp = boost::chrono::time_point<boost::chrono::system_clock, boost::chrono::milliseconds>;
format_to(out, "{}",   tp(boost::chrono::milliseconds(1500)));   // -> 1500ms since epoch
format_to(out, "{:c}", tp(boost::chrono::milliseconds(1500)));   // -> 1500 since epoch
```

### `boost::gregorian::date`
- **Spec:** none. `YYYY-MM-DD` (year min 4 digits); specials `not-a-date`, `+infinity`, `-infinity`.

```cpp
format_to(out, "{}", date(2024,3,5));          // -> 2024-03-05
format_to(out, "{}", date(not_a_date_time));   // -> not-a-date
format_to(out, "{}", date(pos_infin));         // -> +infinity
format_to(out, "{}", date(neg_infin));         // -> -infinity
```

### `boost::posix_time::time_duration`, `boost::posix_time::ptime`
- **Spec:** none. time_duration: `[-]HH:MM:SS[.frac]`, hours ≥ 2 digits (may exceed 99), fraction printed only when non-zero, with `num_fractional_digits()` digits (**6** with default microsecond resolution; 9 with nanosecond build); specials `not-a-date-time`, `+infinity`, `-infinity`. ptime: `YYYY-MM-DDTHH:MM:SS[.frac]` (`T` separator), specials as above.

```cpp
format_to(out, "{}", time_duration(1,2,3));                          // -> 01:02:03
format_to(out, "{}", time_duration(milliseconds(1500)));             // -> 00:00:01.500000
format_to(out, "{}", -time_duration(1,2,3));                         // -> -01:02:03
format_to(out, "{}", -time_duration(milliseconds(500)));             // -> -00:00:00.500000
format_to(out, "{}", time_duration(hours(100)));                     // -> 100:00:00
format_to(out, "{}", time_duration(not_a_date_time));                // -> not-a-date-time
format_to(out, "{}", ptime(date(2024,3,5), time_duration(13,4,5)));  // -> 2024-03-05T13:04:05
format_to(out, "{}", ptime(date(2024,3,5), time_duration(milliseconds(1)))); // -> 2024-03-05T00:00:00.001000
format_to(out, "{}", ptime(pos_infin));                              // -> +infinity
```
- **Notes:** `boost::posix_time::milliseconds(…)`/`microseconds(…)`/`hours(…)` are *subclasses* (`subsecond_duration`/`hours`), **not** `time_duration`: formatter lookup is exact-type, so passing them directly fails (`incomplete type`). Wrap: `time_duration(milliseconds(1500))`; only the exact type `time_duration` matches (same for `hours(100)`, `microseconds(5)`).

---

## `boost_values.hpp` — dynamic_bitset, rational, tribool

### `boost::dynamic_bitset<Block,Alloc>`
- **Spec:** every char scanned, any order, last writer wins: `x`/`X` → hex (`X` = uppercase), `b` → binary, `#` → prefix. Default binary, MSB first. Width/zero-pad not supported.

| Token | Meaning |
|---|---|
| `b` | binary (default) |
| `x` / `X` | hex lowercase / uppercase (⌈size/4⌉ nibbles — leading zeros kept) |
| `#` | prefix `0b`, `0x`, or `0X` (uppercase only when `X`) |

- Empty bitset prints `0` (prefix still emitted if `#`).

```cpp
boost::dynamic_bitset<> bs(10, 0x2A5);
format_to(out, "{}", bs);        // -> 1010100101
format_to(out, "{:x}", bs);      // -> 2a5
format_to(out, "{:#X}", bs);     // -> 0X2A5
format_to(out, "{:#b}", bs);     // -> 0b1010100101
format_to(out, "{}", boost::dynamic_bitset<>());      // -> 0
format_to(out, "{:#x}", boost::dynamic_bitset<>());   // -> 0x0
format_to(out, "{:x}", boost::dynamic_bitset<>(5, 0x1f)); // -> 1f
```

### `boost::rational<IntType>`
- **Spec:** `{:<int-spec>}` applied to numerator **and** denominator. Output `num/den` (never reduced beyond boost's own normalization; negative sign on numerator).

```cpp
format_to(out, "{}", boost::rational<int>(3, 6));    // -> 1/2
format_to(out, "{:03}", boost::rational<int>(3, 4)); // -> 003/004
format_to(out, "{:x}", boost::rational<int>(255,16));// -> ff/10
format_to(out, "{}", boost::rational<int>(-1, 3));   // -> -1/3
```

### `boost::logic::tribool`
- **Spec:** none. `true` / `false` / `indeterminate`.

```cpp
format_to(out, "{} {} {}", tribool(true), tribool(false), tribool(indeterminate)); // -> true false indeterminate
```
- **Notes:** no Boost.Multiprecision formatter exists in this header (docs claim otherwise).

---

## `reloco.hpp` — `jplcz_reloco` types (`{}`/Display-style)

**Include:** `<microfmt/formatters/reloco.hpp>` (also pulls `floating.hpp` and `variant.hpp`). Containers/wrappers use `element_formatter` for elements → `formatter`, then `Display`, then `Debug` of the element.

### Family 1 — sequence containers → `[a, b, c]`
`reloco::vector<T>`, `inline_vector<T,Cap>`, `sso_vector<T,N>`, `outline_vector<T>`, `external_vector<T>`, `boxed_slice<T>`, `vec_deque<T>`, `inline_vec_deque<T,Cap>`, `sso_vec_deque<T,N>`, `outline_vec_deque<T>`, `binary_heap<T,Cmp>`, `flat_set<T,Cmp>`, `inline_flat_set`, `sso_flat_set`, `tree_set<T,Cmp>`, `flat_hash_set<T,H,Eq>`, ring buffers (`ring_buffer<T>`, `outline_ring_buffer<T>`, `inline_ring_buffer<T,N>`, `sso_ring_buffer<T,N>`, `ring_buffer_ref<T>`: oldest→newest), type-erased `collection_view<T>` and `mutable_sequence_container_ref<T>` (via `as_collection_view(c)`).
- **Spec:** `{:<elem-spec>}` forwarded to every element. **No delimiter flags** (always `[ ]`, sep `", "`).
- **Order:** sets sorted (flat/tree); `flat_hash_set` bucket order (unspecified); `binary_heap` = internal heap order (not sorted); deques front→back.

```cpp
reloco::vector<int> vec{10, 255};
format_to(out, "{}", vec);                        // -> [10, 255]
format_to(out, "{:04X}", vec);                    // -> [000A, 00FF]
format_to(out, "{:#x}", vec);                     // -> [0xa, 0xff]
format_to(out, "{}", reloco::vector<int>{});      // -> []
format_to(out, "{}", as_collection_view(vec));    // -> [10, 255]
format_to(out, "{:x}", as_collection_view(vec));  // -> [a, ff]
format_to(out, "{:02}", flat_set{5,2});           // -> [02, 05]
format_to(out, "{}", heap /* pushed 5,1,9 */);    // -> [9, 1, 5]
format_to(out, "{}", dq /* push_back 1, push_front 0 */); // -> [0, 1]
format_to(out, "{}", tree_set{3,1});              // -> [1, 3]
format_to(out, "{}", ring /* wrote 1,2,3 */);     // -> [1, 2, 3]
```

### Family 2 — map containers → `{k: v, k: v}`
`flat_map<K,V,Cmp>`, `inline_flat_map<K,V,Cap,Cmp>`, `sso_flat_map`, `tree_map<K,V,Cmp>`, `flat_hash_map<K,V,H,Eq>`, `lru_cache<K,V,H,Eq>` (most→least recently used, does not promote), `mutable_associative_container_ref<T,Key>`.
- **Spec:** `{:<spec>}` forwarded to **both keys and values** for all concrete maps; for `mutable_associative_container_ref` (type-erased) it applies to **values only**. No delimiter flags.

```cpp
reloco::flat_map<int,int> fm{{2,255},{1,10}};
format_to(out, "{}", fm);                 // -> {1: 10, 2: 255}
format_to(out, "{:x}", fm);               // -> {1: a, 2: ff}
format_to(out, "{:03}", tree_map);        // -> {001: 010, 002: 020}
format_to(out, "{}", lru /* put 1,"one"; put 2,"two" */); // -> {2: two, 1: one}
format_to(out, "{}", flat_hash_map{{1,2}}); // -> {1: 2}
```

### Family 3 — strings and obfuscated strings (text written raw)
`reloco::basic_string<C,T>` (`string`), `basic_inline_string<Cap,C,T>`, `basic_sso_string<C,T>`, `obfuscated_string<N>`, `obfuscated_string_ref`, `obfuscated_decrypted_view<N>` (from `RELOCO_OBFUSCATED_STR`).
- **Spec (strings):** same as `microfmt::string_view` → only `?` (debug quote + escape; non-ASCII bytes escaped as `\xNN`). No width/fill.
- **Spec (`obfuscated_string<N>`, `obfuscated_string_ref`):** none; `{:?}` and `{:10}` **do not quote/pad** — raw decoded bytes are streamed one at a time, never materialized.
- **Spec (`obfuscated_decrypted_view<N>`):** as `string_view` (`?` quotes).

```cpp
reloco::string rs = ...("héllo");
format_to(out, "{} | {:?}", rs, rs);                    // -> héllo | "h\xc3\xa9llo"
format_to(out, "{:?}", inline_string_8_holding_"ab\n"); // -> "ab\n"
auto ob = reloco::obfuscated_string("secret", UINT64_C(0x0123456789abcdef));
format_to(out, "{} {:?} {:10}", ob, ob, ob);            // -> secret secret secret
auto dv = RELOCO_OBFUSCATED_STR("plain");
format_to(out, "{} {:?}", dv, dv);                      // -> plain "plain"
```

### Family 4 — smart pointers / borrow wrappers (print pointee, not address)
`value_ptr<T>` (null → `(null)`), `value_ref<T>` (never null), `reloco::unique_ptr<T>` / `shared_ptr<T>` / `rc<T>` (null → `(null)`), `weak_ptr<T>` / `weak_rc<T>` (lock(); expired → `(expired)`), `cow<T>` (borrowed or owned, no decoration), `cell<T>`, `packed_bits<T>` (prints `value()` via `formatter<T>`).
- **Spec:** `{:<spec>}` forwarded to pointee (`value_ptr`, `unique_ptr`, `shared_ptr`, `rc`, `weak_*`, `cow`, `cell`, `value_ref` via element_formatter; `packed_bits` via `formatter<T>`). `(null)`/`(expired)` ignore the spec.

```cpp
format_to(out, "{} {}", value_ptr<int>(&target), value_ptr<int>(nullptr)); // -> 5 (null)
format_to(out, "{:03}", value_ref<int>(target));                           // -> 005
format_to(out, "{} {}", up, reloco::unique_ptr<int>());                    // -> 99 (null)
format_to(out, "{} {}", sp, wp);                                           // -> 8 8
format_to(out, "{} {} {}", rcp, wrc, reloco::rc<int>());                   // -> 8 8 (null)
format_to(out, "{}", wrc /* after all rc released */);                     // -> (expired)
format_to(out, "{:04}", cow_borrowing_42);                                 // -> 0042
format_to(out, "{:03}", reloco::cell<int>(5));                             // -> 005
format_to(out, "{:x} {}", reloco::packed_bits<unsigned>(255u), reloco::packed_bits<uint8_t>(7)); // -> ff 7
```

### Family 5 — numeric wrappers
`non_zero<T>`, `saturating<T>`, `wrapping<T>`, `checked<T>` → `formatter<T>` of the wrapped value; spec forwarded. (`checked_value<T>` is in `monad.hpp`.)

```cpp
format_to(out, "{}", reloco::non_zero<int>::try_create(7).value());     // -> 7
format_to(out, "{:x}", reloco::saturating<uint8_t>(200));               // -> c8
format_to(out, "{}", reloco::wrapping<uint8_t>(250));                   // -> 250
format_to(out, "{}", reloco::checked<int>(13));                         // -> 13
```

### `reloco::fixed_point<Rep, FracBits>` (arithmetic `Rep`) and wide ints
- **fixed_point spec:** `{:<float-spec>}` forwarded to `formatter<double>` of `raw / 2^FracBits` (`.Nf`, `e`, …; leading width rejected → default).

```cpp
auto fp = reloco::fixed_point<std::int32_t, 8>::from_raw(384);
format_to(out, "{} {:.1f} {:.3f} {:8.2f}", fp, fp, fp, fp);   // -> 1.5 1.5 1.500 1.5   (width spec not honored)
```
- **`reloco::fixed_int<N>` / `fixed_uint<N>` wide (`detail::wide_int<N,Signed>`, widths beyond native) and non-integral `__int128`:** int spec `[#][0][width][x|X]` (same as built-in ints: `#` → `0x`/`0X` prefix, `0` zero-pad, decimal width ≤255, `x`/`X` hex). Width counts the prefix.

```cpp
using i256 = reloco::fixed_int<256>; using u256 = reloco::fixed_uint<256>;
format_to(out, "{} {} {:#x} {:06} {:X}", i256(-42), u256(0), u256(255), i256(7), u256(255)); // -> -42 0 0xff 000007 FF
format_to(out, "{:#08x} {:8}|", u256(255), i256(5));       // -> 0x0000ff        5|
format_to(out, "{}", ~u256(0));    // -> 115792089237316195423570985008687907853269984665640564039457584007913129639935
format_to(out, "{}", reloco::fixed_int<128>(1) << 100U);   // -> 1267650600228229401496703205376
```

### Family 6 — time, ordering, identity, error, location, pointers
| Type | Spec | Output |
|---|---|---|
| `reloco::duration` | **every char scanned**, last precision wins: `m`/`3` → 3 digits, `u`/`6` → 6, `n`/`9` → 9 (default 9); `r`/`R` → suppress trailing `s`; other chars (incl. other digits) ignored | `<secs>.<frac>s` |
| `reloco::instant` | none | `HH:MM:SS.mmm` since default-constructed `instant` (hours not wrapped, ≥2 digits) |
| `reloco::ordering` | none | `less` / `equal` / `greater` (`unknown` if invalid) |
| `reloco::type_id` | none | registered name (`RELOCO_TYPE_ID_NAME`/implicit typeid), `<unnamed type>`, or `<no type>` for default |
| `reloco::error` | none | enum member name (e.g. `out_of_range`, `page_fault`); unknown → `error(N)` |
| `reloco::call_location` | none | `file:line`; `file==nullptr` → `<unknown>:line` |
| `reloco::call_location_ref` | none | `file:line` or `<no location>` |
| `reloco::masked_pointer<T,Auth>` | none | `(null)` or raw address `0x…` (authenticates first) |
| `reloco::variant<Ts...>` | forwarded to `formatter<std::variant<Ts...>>` | active alternative's text, e.g. `3` |

```cpp
format_to(out, "{}",    reloco::duration::from_nanos(1'234'567'890ULL)); // -> 1.234567890s
format_to(out, "{:m}",  reloco::duration::from_millis(1500));            // -> 1.500s
format_to(out, "{:6r}", reloco::duration::from_micros(2000250ULL));      // -> 2.000250
format_to(out, "{:3u}", reloco::duration::from_micros(2000250ULL));      // -> 2.000250s   ('u' after '3' wins)
format_to(out, "{:R}",  reloco::duration::from_secs(5));                 // -> 5.000000000
format_to(out, "{:q}",  reloco::duration::from_secs(5));                 // -> 5.000000000s
format_to(out, "{}", reloco::instant() + reloco::duration::from_millis(3'661'250ULL)); // -> 01:01:01.250
format_to(out, "{}", reloco::instant() + reloco::duration::from_secs(360000));          // -> 100:00:00.000
format_to(out, "{} {} {}", reloco::ordering::less, reloco::ordering::equal, reloco::ordering::greater); // -> less equal greater
format_to(out, "{} {} {}", type_id::of<int>(), type_id{}, type_id::of<struct unn>());   // -> int <no type> <unnamed type>
format_to(out, "{} {}", reloco::error::out_of_range, reloco::error::page_fault);        // -> out_of_range page_fault
format_to(out, "{}", static_cast<reloco::error>(250));                   // -> error(250)
format_to(out, "{} {}", reloco::call_location{"a.cpp", 12}, reloco::call_location{nullptr, 3}); // -> a.cpp:12 <unknown>:3
format_to(out, "{} {}", reloco::call_location_ref{}, reloco::call_location_ref::current());     // -> <no location> d.cpp:112
format_to(out, "{}", reloco::masked_pointer<int>());                     // -> (null)
format_to(out, "{}", reloco::masked_pointer<int>(&x));                   // -> 0x... (address)
format_to(out, "{}", reloco::variant<int,bool>{3});                      // -> 3
```
- **Notes:** `reloco::function_ref` has no formatter by design. `reloco::error` valid values: allocation_failed … page_fault (one name per enumerator, 31 cases).

---

## `reloco_debug.hpp` — `reloco::Debug<T>` (used by `{:?}` / `as_debug()` and as `{}` fallback)

- **Include:** `<microfmt/formatters/reloco_debug.hpp>` (also needs reloco `any`, `error`, `flat_hash_*`, `rc`). Specializes only `reloco::Debug<T>`; no spec accepted (they are `static format(val, sink)`); invoke with `microfmt::as_debug(x)` (lvalues only — rvalue `as_debug` is deleted) or `{:?}` where the element formatter falls back to Debug.
- **Entries:**

| Type | Debug output |
|---|---|
| `reloco::any` | `any(<type name>)` via `formatter<type_id>`: `any(int)`, `any(<unnamed type>)`, `any(<no type>)`. Held value is never printed. |
| `reloco::error` | member name (same as `formatter<error>`) |
| `reloco::expected<T,E>` / `result<T>` | `Ok(<as_debug(v)>)` / `Err(<as_debug(e)>)` |
| `reloco::expected<void,E>` | `Ok()` / `Err(<as_debug(e)>)` |
| `reloco::rc<T>` | `rc(use_count: N) { <as_debug(*p)> }`; empty → `rc(use_count: 0) { (null) }` |
| `reloco::weak_rc<T>` | `rc(use_count: N) { <value> }`; expired → `rc(use_count: 0) { (expired) }` |
| `reloco::flat_hash_set<T,H,Eq>` | `flat_hash_set(size: S, capacity: C, load_factor_permille: P) [e1, e2]` |
| `reloco::flat_hash_map<K,V,H,Eq>` | `flat_hash_map(size: S, capacity: C, load_factor_permille: P) {k: v, k: v}` |

```cpp
format_to(out, "{}", as_debug(any_empty));                 // -> any(<no type>)
format_to(out, "{}", as_debug(any_holding_int));           // -> any(int)
format_to(out, "{}", as_debug(any_holding_unnamed));       // -> any(<unnamed type>)
format_to(out, "{} {}", as_debug(res_ok5), as_debug(res_notfound)); // -> Ok(5) Err(not_found)
format_to(out, "{} {}", as_debug(result_void_ok), as_debug(result_void_timeout)); // -> Ok() Err(timed_out)
format_to(out, "{}", as_debug(error_timed_out));           // -> timed_out
format_to(out, "{}", as_debug(rcp));                       // -> rc(use_count: 2) { 8 }
format_to(out, "{}", as_debug(rc_null));                   // -> rc(use_count: 0) { (null) }
format_to(out, "{}", as_debug(wrc_expired));               // -> rc(use_count: 0) { (expired) }
format_to(out, "{}", as_debug(hs));                        // -> flat_hash_set(size: 1, capacity: 8, load_factor_permille: 125) [1]
format_to(out, "{}", as_debug(hm));                        // -> flat_hash_map(size: 1, capacity: 8, load_factor_permille: 125) {1: 2}
format_to(out, "{} {}", rok, rer);   // plain {} on reloco::result without this header's Debug also -> Ok(5) Err(not_found)
```
- **Notes:** `{}` on `reloco::result<T>` produces `Ok(..)/Err(..)` because `Debug<expected>` is the fallback. Other reloco containers/pointers need no `Debug` specialization: they recurse into element `Debug<T>` through `element_formatter`.

---

## Quick index

| Want | Use | Spec |
|---|---|---|
| comma/delimiter list of a range | `join(range, "sep")`, `join(first,last,"sep")` | `{:<elem>}` |
| compile-time delimiter/elem spec (C++20) | `join_as<"sep","elem">(range)` | runtime `{:<elem>}` only if `ElemSpec==""` |
| `index: value` list | `enumerate(range, "sep")` | `{:<elem>}` |
| paired ranges `(a, b)` | `zip(a, b, "sep")` | `{:<elem>}` |
| only matching elements | `filter(range, pred)`, `filter(ptr, n, pred)` | `{:[b\|c\|n]<elem>}` default `[ ]` |
| repeat a value | `repeat(v, n[, "sep"])` | `{:<spec>}` whole spec to value |
| map `k: v` entries | `map_view(c)`, `map_view(c, kf, vf)`, `map_view(it, end[, kf, vf])` | `{:[b\|c\|n]<elem>}` default `{ }`, elem → keys+values |
| tuple / pair | `std::tuple`, `std::pair` (direct) | `{:[b\|c\|n\|p]<elem>}` default `( )` |
| vector / 3-vector / matrix | `vec(arr)`, `vec3(x,y,z)`, `mat<T,R,C>(arr)` | `{:<elem>}` |
| optional / expected / checked | `std::optional`, `reloco::optional`, `expected`, `std::expected`, `checked_value` | `{:<inner>}` → `Some(..)/None`, `Ok(..)/Err(..)`, bare / `<moved-from>` |
| enum / struct by P2996 | enums auto; `MICROFMT_REFLECT_FORMAT(T)` | none; `name` / `{x: 1, y: 2}` |
| enum / struct by Boost.Describe | `BOOST_DESCRIBE_ENUM/STRUCT` | none; `name` or `static_cast<uintN_t>(v)` / `{x: 1, y: 2}` |
| `boost::container::static_vector/small_vector` | direct | `{:<elem>}` → `[a, b]` |
| `boost::optional` | direct | `{:<inner>}` → `Some(..)/None` |
| `boost::variant2::variant` | direct | `{:[i\|#]<alt>}` → `v` or `variant[idx](v)` |
| `boost::outcome` result | direct | `{:<inner>}` → `Ok(..)/Err(..)/NoValue` |
| Asio IPs / endpoints | `address_v4/v6`, `address`, `basic_endpoint` | none |
| Boost.System code/condition | `error_code`, `error_condition` | none → `cat:value` |
| Boost.Chrono duration / time_point | direct | `c` anywhere hides unit |
| Boost.DateTime | `gregorian::date`, `posix_time::time_duration`, `ptime` (exact types only) | none |
| `boost::dynamic_bitset` | direct | chars `b`, `x`, `X`, `#` (any order) |
| `boost::rational` | direct | `{:<int-spec>}` on both parts → `n/d` |
| `boost::tribool` | direct | none |
| reloco sequences/sets/deques/rings | direct | `{:<elem>}` → `[..]` |
| reloco maps / lru_cache | direct | `{:<spec>}` → keys+values → `{k: v}` |
| reloco strings / inline / sso | direct | `?` only |
| obfuscated strings | `obfuscated_string<N>`, `_ref`, `_decrypted_view<N>` | none (view: `?`) |
| reloco pointers & cow & cell | `value_ptr`, `value_ref`, `unique_ptr`, `shared_ptr`, `rc`, `weak_*`, `cow`, `cell` | `{:<pointee spec>}`; `(null)`/`(expired)` |
| reloco numeric wrappers | `non_zero`, `saturating`, `wrapping`, `checked`, `packed_bits` | `{:<int spec>}` |
| `fixed_point` | `reloco::fixed_point<Rep,Bits>` | `{:.Nf}`/`e` (no width) |
| wide ints | `fixed_int<N>`, `fixed_uint<N>`, `__int128` (strict std) | `[#][0][width][x\|X]` |
| time | `reloco::duration` / `instant` | `m/3 u/6 n/9 r/R` / none |
| misc reloco | `ordering`, `type_id`, `error`, `call_location(_ref)`, `masked_pointer`, `variant` | none |
| debug dump with diagnostics | `as_debug(any/rc/weak_rc/flat_hash_*/result)` | none |
