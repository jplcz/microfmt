<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Cheatsheet: time, units, inspector, markdown, and logging

Part of the [formatter cheatsheet](../cheatsheet.md). All examples were compiled with `g++ -std=c++20 -I include -I <reloco>/include` and the output shown is real.
Printing helper used: `auto out = microfmt::stdout_sink(); microfmt::println(out, "...", ...);` (`<microfmt/sinks/stdio.hpp>`).

---

## include/microfmt/formatters/chrono.hpp

### `std::chrono::duration<Rep, Period>`
**Include:** `<microfmt/formatters/chrono.hpp>`
**Spec:** any `c` or `C` character in the spec hides the unit suffix. All other characters are ignored (`{:x}` still prints the suffix).

| Period | Suffix |
|---|---|
| nano / micro / milli / ratio<1> | `ns` / `us` / `ms` / `s` |
| ratio<60> / ratio<3600> / ratio<86400> | `min` / `h` / `d` |
| anything else | ` [custom]` (e.g. `duration<int,ratio<1,100>>(7)` → `7 [custom]`) |

**Example:** `{}` of `milliseconds(1500)` → `1500ms`; `seconds(-5)` → `-5s`; `minutes(2)` → `2min`; `hours(3)` → `3h`; `days(4)` → `4d`; `nanoseconds(1)` → `1ns`; `{:c}` of `milliseconds(1500)` → `1500`; `{:c}` of `seconds(-5)` → `-5`; `{:x}` of `seconds(9)` → `9s`.
**Notes:** The representation is cast to `uint64_t` for printing (negative signed values print `-N`), so a floating-point `Rep` is truncated. No width/fill.

### `std::chrono::system_clock::time_point` (any duration)
**Spec:** characters are scanned: `t`/`T` = time only, `d`/`D` = date only. No spec = both.

| Spec | Output for 1715420000123 ms since epoch |
|---|---|
| `{}` | `2024-05-11T09:33:20.123Z` |
| `{:t}` / `{:T}` | `09:33:20.123Z` |
| `{:d}` / `{:D}` | `2024-05-11` |

`time_point<system_clock, seconds>` of 365 days → `1971-01-01T00:00:00.000Z` (the milliseconds field is always printed).
**Notes / bugs:**
- `{:td}` (both flags) prints the full date and time, same as `{}`.
- Pre-epoch timestamps use floor semantics: `time_point{milliseconds(-1500)}` prints `1969-12-31T23:59:58.500Z`.
- Always UTC with a `Z`.

### `std::chrono::steady_clock::time_point`
**Spec:** none (ignored). Prints uptime `HH:MM:SS.mmm`; hours are not wrapped at 24. `time_point{milliseconds(3723004)}` → `01:02:03.004`.

---

## include/microfmt/formatters/chrono_calendar.hpp

**Include:** `<microfmt/formatters/chrono_calendar.hpp>` — only available when `MICROFMT_HAS_CHRONO_CALENDAR` (`__cpp_lib_chrono >= 201907L`, C++20 calendar types).

| Type | Spec | Example → output |
|---|---|---|
| `std::chrono::year` | none | `2026` ; `year(-5)` → `-0005` |
| `std::chrono::month` | none, or exactly `s` | `{}` → `03`; `{:s}` → `Mar`; invalid month 13 → `13` even with `{:s}` |
| `std::chrono::day` | none | `07` |
| `std::chrono::weekday` | none | `weekday(1)` → `Mon`; `Sunday` → `Sun`; `weekday(7)` → `Sun`; not `ok()` → `?` |
| `std::chrono::year_month_day` | none | `2026-03-07`; Feb 31 → `2026-02-31 (invalid)` |
| `std::chrono::hh_mm_ss<D>` | none | see below |

`hh_mm_ss`: `milliseconds(3723004)` → `01:02:03.004`; `seconds(-61)` → `-00:01:01`; `microseconds(1500)` → `00:00:00.001500`; `duration<int,ratio<1,3>>(4)` → `00:00:01.333333`; `hours(49)` → `49:00:00` (hours not wrapped). Fraction digits follow the sub-second precision of the duration (none for whole seconds).
**Notes:** Only `month` accepts a spec.

---

## include/microfmt/formatters/bintime.hpp

### Generic binary-time formatter (any `T` with `.sec` and `.frac`)
**Include:** `<microfmt/formatters/bintime.hpp>`
**Types:** `is_bintime<T>` trait / `is_bintime_v<T>` (structural: has `.sec` and `.frac`; specializable); `enum class time_precision : uint8_t { sec=0, ms=3, us=6, ns=9, ps=12 }`. `frac` is a 64-bit fraction in units of 2^-64.
**Spec (chars scanned, last precision char wins):**

| Char | Meaning |
|---|---|
| `m` / `3` | 3 digits (ms) |
| `u` / `6` | 6 digits (us) |
| `n` / `9` | 9 digits (ns) — default |
| `p` / `1` | 12 digits (ps) |
| `r` / `R` | raw: omit the trailing `s` |

**Example** (`sec=1715420000, frac=0x8000000000000000`): `{}` → `1715420000.500000000s`; `{:m}` and `{:3}` → `1715420000.500s`; `{:u}` → `1715420000.500000s`; `{:p}` → `1715420000.500000000000s`; `{:r}` → `1715420000.500000000`; `{:mr}` → `1715420000.500`. `{sec=-3, frac=0x4000...}` → `-3.250000000s`.
**Notes:** Fractions are truncated, not rounded. The `sec` precision is not reachable through the spec.

### `sbintime_view<T>` via `as_sbintime(integral)`
Wraps an int64 in 32.32 fixed point (seconds in the high 32 bits).
**Spec:** `m`/`3`, `u`/`6`, `n`/`9`, `r`/`R`; default **us (6 digits)**. `p`/`1` are NOT supported (ignored, stays us).
**Example** (raw value for 5.5 s): `{}` → `5.500000s`; `{:m}` → `5.500s`; `{:n}` → `5.500000000s`; `{:p}` → `5.500000s`; `{:r}` → `5.500000`; `{:3r}` → `5.500`; negative → `-5.500000s`.

---

## include/microfmt/formatters/posix_time.hpp

**Include:** `<microfmt/formatters/posix_time.hpp>`
**Types:** any struct with `tv_sec`+`tv_nsec` (`is_timespec_v`) or `tv_sec`+`tv_usec` (`is_timeval_v`). A leading `:` in the spec is tolerated.

| Type | Default | Spec |
|---|---|---|
| `timespec` | 9 digits + `s` | `m`/`3`, `u`/`6`, `n`/`9`, `r`/`R` (no `s`); `p` ignored |
| `timeval` | 6 digits + `s` | `m`/`3`, `u`/`6`, `r`/`R`; `n` ignored |

**Example:** `timespec{1715420000,123456789}`: `{}` → `1715420000.123456789s`; `{:m}` → `1715420000.123s`; `{:u}` → `1715420000.123456s`; `{:r}` → `1715420000.123456789`. `timeval{1715420000,123456}`: `{}` → `1715420000.123456s`; `{:m}` → `...123s`; `{:r}` → `1715420000.123456`; `{:3r}` → `1715420000.123`.
**Notes:** A negative `tv_sec` with a nonzero fraction is normalized to the true value: `timespec{-2,5}` → `-1.999999995s`, `timeval{-3,7}` → `-2.999993s`.

---

## include/microfmt/formatters/units.hpp

**Include:** `<microfmt/formatters/units.hpp>`. It does **not** include the formatters for the wrapped value, so add `floating.hpp` / `string.hpp` yourself when wrapping doubles/strings.
**Types and factories** (all `constexpr [[nodiscard]]`):

```cpp
enum class scale_base { decimal = 1000, binary = 1024 };
template <class T> struct auto_unit_view { T value; string_view unit_symbol; scale_base base; uint8_t precision; };
template <class T> struct unit_view      { T value; string_view unit_symbol; };
auto_unit_view<T> auto_si(T integral, string_view unit, uint8_t precision = 2);   // k M G T P
auto_unit_view<T> auto_bytes(T integral, uint8_t precision = 2);                  // "B", base 1024: Ki Mi Gi Ti Pi
auto_unit_view<T> hertz(T integral, uint8_t precision = 2);                       // "Hz", SI prefixes
unit_view<T>      with_unit(T val, string_view unit);                             // any T with a formatter
```

### `auto_unit_view<T>` (`auto_si`, `auto_bytes`, `hertz`)
**Spec:** `.D` where `D` is a decimal number (0-15, larger values are capped) that overrides the factory precision (`{:.12}` is 12 decimals). Other characters are ignored. Width/alignment are not applied to the whole text.
**Example:** `hertz(50000000)` → `50.00 MHz`; `hertz(999)` → `999 Hz`; `{:.0}` of 50000000 → `50 MHz`; `{:.3}` of 1234567 → `1.234 MHz`; `auto_bytes(1048576)` → `1.00 MiB`; `1536` → `1.50 KiB`; `{:.1}` → `1.5 KiB`; `512` → `512 B`; `auto_bytes(1536,0)` → `1 KiB`; `auto_bytes(-2048)` → `-2.00 KiB`; `auto_si(42,"V")` → `42 V`; `auto_si(2500,"W",3)` with `{:.1}` → `2.5 kW`; `auto_si(-1500,"A",1)` → `-1.5 kA`.
**Notes:** Fraction is truncated (not rounded) and printed only when scaled (prefix > none). Max prefix is P (`2^60 B` → `1024.00 PiB`). One space separates value and unit.

### `unit_view<T>` (`with_unit`)
**Spec:** the WHOLE spec is forwarded to `formatter<T>`, then ` unit` is appended.
**Example:** `with_unit(42,"mA")` → `42 mA`; `{:5}` → `   42 mA`; `with_unit(255,"B")` `{:x}` → `ff B`, `{:#x}` → `0xff B`; `with_unit("hi","x")` → `hi x`.
**Notes:** The spec applies to the *value only*, so a width pads the number and the unit follows (`{:5}` -> `   42 mA`); there is no whole-text width. `{<5}`/`{>5}` alignment characters are not understood by the int formatter, and `{:08.3f}` works on doubles when `floating.hpp` is included.

---

## include/microfmt/formatters/source_location.hpp

**Include:** `<microfmt/formatters/source_location.hpp>`. Enabled when `MICROFMT_HAS_SOURCE_LOCATION`: `std::source_location` needs C++20 and `__cpp_lib_source_location`; the Boost variant needs `#define MICROFMT_ENABLE_BOOST_SOURCE_LOCATION` (and `<boost/assert/source_location.hpp>`).
**Types/factories:**

```cpp
template <class Loc> struct basic_source_loc_view { Loc loc; bool show_function = true; bool file_basename_only = true; };
using std_source_loc_view = ...; using boost_source_loc_view = ...; using source_loc_view = ...; // whichever is enabled
auto source_loc(std::source_location loc = std::source_location::current(), bool show_fn = true, bool basename_only = true);
auto source_loc(boost::source_location loc, bool show_fn = true, bool basename_only = true); // no default
```
Direct formatters for `std::source_location` and `boost::source_location` take no spec (function shown, basename only).
**Spec (view):** `s`/`S` = `file:line` only (hide function); `f`/`F` is a no-op (the function is already shown).
**Example (gcc):** `{}` of `source_loc()` → `t1.cpp:74 in int main()`; `{:s}` → `t1.cpp:77`; `source_loc(loc,false,true)` → `t1.cpp:74`.
**Notes:** The function name is printed as `function_name()` plus an added `()`, so gcc (whose `function_name()` already contains parentheses) yields a doubled `()`. Basename strips at the last `/` or `\`.

---

## include/microfmt/inspector/ (formatters)

Common setup for the examples below. A tiny in-memory "guest" address space, mapped at virtual 0x1000:

```cpp
#include <microfmt/inspector/address_space.hpp>   // + the specific headers named per formatter
struct guest_tag {};
struct guest_mem { uintptr_t base; uint8_t* data; size_t size; };
template <> struct microfmt::address_space_traits<guest_tag> {
  using context_type = guest_mem;
  static bool read_bytes(microfmt::value_ref<const guest_mem> c, uintptr_t a, void* d, size_t n) noexcept {
    if (a < c->base || a - c->base + n > c->size) return false;
    std::memcpy(d, c->data + (a - c->base), n); return true; }
  static bool read_string(microfmt::value_ref<const guest_mem> c, uintptr_t a, char* d, size_t max,
                          size_t& out_len, bool& null_term) noexcept { /* copy up to max, stop at NUL */ }
};
static uint8_t ram[0x800]; static guest_mem gm{0x1000, ram, sizeof ram};
microfmt::address_space_ref sp(guest_tag{}, gm);   // handle; built-in: address_space_ref(local_space_tag{}) reads host memory
```
Scratch buffers for typed reads must be aligned: `alignas(8) std::byte scratch[N];`. None of the inspector formatters (except where stated) accept a spec.
Guest contents used: `"hello world"` at 0x1000; `Point{int x,y}` {3,-4} at 0x1100 with a user `formatter<Point>` (`Point(3,-4)`; spec `x` → hex).

### `remote_string_view` (address_space.hpp)
**Ctor:** `remote_string_view(uintptr_t addr, address_space_ref, span<char> scratch | char(&)[N], size_t max_limit = 4096)`
**Spec:** none (`{:>20}` ignored).

```cpp
char buf[16];
println(out, "{}", remote_string_view(0x1000, sp, buf));        // hello world
```
Outputs: normal `hello world`; small scratch (4 chars) still `hello world` (read in chunks); `max_limit=5` → `hello...`; null address → `(null)`; unmapped → `<fault@0x9000>`; empty `address_space_ref{}` or empty scratch → `<invalid-space@0x1000>`; string running into unmapped memory → `qqqqqqqq<fault>` (first-read fault prints `<fault@0xADDR>`, later `<fault>`).
**Notes:** `...` is appended when total length reaches `max_limit` — also when the string ends exactly there (`max_limit=11` on `hello world` → `hello world...`; 12 → `hello world`).

### `foreign_string_view`, `string_ptr`, `string32_ptr` (foreign_string_view.hpp)
Same as `remote_string_view` but the invalid-space text is `<invalid-foreign@0x1000>`. Created via `string_ptr::as_view(space, scratch, max)` / `string32_ptr::as_view` (the rvalue overloads are deleted — keep the pointer in a named `const` variable) or directly `foreign_string_view(addr, sp, buf, max)`.
**Examples:** `foreign_string_view(0x1000, sp, buf)` → `hello world`; max 3 → `hel...`; null → `(null)`; fault → `<fault@0x9000>`.
**Host-side holders:** `string_ptr` (8-byte raw address) prints `0x1000` / `(null)`; `string32_ptr` (4-byte) prints `0x00001000` / `(null)`. Inside a `MICROFMT_REMOTE_STRUCT` they are dereferenced (see `remote_object_view`).

### `compat32_ptr<T>` and 32-bit helpers (compat32.hpp)
`explicit compat32_ptr<T>(uint32_t)`; prints `0x00001234` (`{:#010x}`) or `(null)`; no spec.
Factories: `make_remote_ref32(compat32_ptr<T>, space, std::byte(&)[N])` → a `remote_ref<T>`; `make_remote_string32(compat32_ptr<const char>, space, char(&)[N], size_t max = 4096)` → `hello world`.

### `remote_ref<T>` (address_space.hpp)
**Ctor:** `remote_ref<T>(uintptr_t addr, address_space_ref, span<std::byte> | std::byte(&)[N])`.
**Spec:** the whole spec is forwarded to `formatter<T>`.
Outputs: `{}` → `Point(3,-4)`; `{:x}` → `Point(0x3,-0x4)`; `remote_ref<uint32_t>` of 255: `{:#x}` → `0xff`, `{:08b}` → `11111111`; null → `(null)`; unmapped → `<fault@0x9000>`; scratch smaller than `sizeof(T)` (or misaligned) → `<fault@0x1100>`.

### `remote_basic_string_view<Layout>` (remote_basic_string.hpp)
**Factory:** `make_remote_basic_string_view(uintptr_t obj_addr, address_space_ref, span<char>|char(&)[N], Layout, size_t max_limit = 4096)`; no spec.
**Layouts** (`remote_basic_string_traits`):
- `pointer_size_layout<RemotePtr = uintptr_t, RemoteSize = size_t>(ptrdiff_t data_offset, ptrdiff_t size_offset)`
- `size_selected_layout<RemotePtr, RemoteSize>(data_offset, size_offset, inline_data_offset, inline_capacity)` (small-string optimisation: inline when size ≤ capacity)
- `callback_layout(size_reader, data_reader)`; readers are `noexcept` lambdas `(address_space_ref, uintptr_t obj, size_t& out)` / `(address_space_ref, uintptr_t obj, uintptr_t& out, size_t size)` returning `bool`.

```cpp
char buf[8];
auto layout = remote_basic_string_traits::pointer_size_layout(0, 8);   // {char* data; size_t size;} at 0x1110
println(out, "{}", make_remote_basic_string_view(0x1110, sp, buf, layout));   // hello world
```
Outputs: `hello world`; `max_limit=4` → `hell...`; null → `(null)`; unmapped → `<fault@0x9000>`; SSO layout (inline `inlin`; heap `hello world`); callback layout → `hello`.

### `remote_container_view` (remote_container.hpp + remote_vector / carray / forward_list / hash_table / binary_tree)
**Build:** `container.view(address_space_ref, span<std::byte> scratch, container_options = {})` (lvalue containers only). No spec.

```cpp
struct container_options { bool print_key = true, print_value = true; string_view kv_separator = ": ", entry_separator = ", ",
                           open_bracket = "{", close_bracket = "}"; size_t max_print = 64; };
make_remote_vector<T, RemotePtr = uintptr_t, RemoteSize = size_t>(addr, data_off, size_off, cap_off = -1)
make_remote_carray<T>(addr, count)
make_remote_forward_list<T, RemotePtr>(addr, head_off, next_off, data_off)
make_remote_hash_table<Key, Value, RemotePtr, RemoteSize>(addr, buckets_ptr_off, bucket_count_off, node_next_off, node_key_off, node_value_off)
make_remote_binary_tree<Key, Value, RemotePtr>(addr, root_off, left_off, right_off, key_off, value_off)
// custom memory layouts: make_remote_vector<Tag>(addr, ctx) etc. with remote_*_traits<Tag>
```
Element types print via a registered `remote_object_traits` (`MICROFMT_REMOTE_STRUCT`) or else `formatter<T>` of a scratch-read copy.
Examples (`alignas(8) std::byte cs[256];`):

| Call | Output |
|---|---|
| vector `{10,20,30,40}` (`make_remote_vector<uint32_t>(0x1200,0,8,16).view(sp,cs)`) | `{10, 20, 30, 40}` |
| `max_print = 2` | `{10, 20, ...}` |
| brackets `[`/`]`, separator `"; "` | `[10; 20; 30; 40]` |
| `make_remote_carray<uint32_t>(0x1240, 3)` | `{10, 20, 30}` |
| `vector<Point>` | `{Point(10,20), Point(30,40), Point(0,0), Point(0,0)}` |
| forward list | `{7, 8}`; `max_print=1` → `{7, ...}`; cycle with `max_print=3` → `{7, 8, 7, ...}` |
| hash table | `{1: 100, 2: 200}`; `print_key=false` → `{100, 200}`; `print_value=false` → `{1: , 2: }`; `kv_separator=" => "`, `<`/`>` → `<1 => 100, 2 => 200>` |
| binary tree (in-order) | `{1: 10, 2: 20, 3: 30}` |
| default-constructed `remote_container_view{}` | `<null>` |
| vector whose object address is unreadable | `{}` (a size-read failure gives empty brackets) |
| scratch too small (vector scratch 2 bytes; tree scratch < 64 bytes) | `{<fault>}` |

**Notes:** An element fault prints `<fault>` and stops. Tree traversal is in-order and needs scratch ≥ 64 bytes (stack depth is capped at 64 and carved from the scratch).

### Smart pointers (remote_smart_ptr.hpp)
No specs. The pointee is printed by `formatter<T>` (or a registered struct).

```cpp
remote_unique_ptr_view<T, RemotePtr = uintptr_t>(uintptr_t ptr_addr, address_space_ref, span<std::byte> scratch, ptrdiff_t ptr_offset = 0)
remote_shared_ptr_view<T, RemotePtr, RemoteRefCount = int32_t>(addr, sp, scratch, ptr_offset, control_block_offset, use_count_offset, weak_count_offset)
remote_intrusive_ptr_view<T, RemotePtr, RemoteRefCount>(addr, sp, scratch, ptr_offset, ref_count_offset)
```
Outputs (pointer object holding 0x1100; control block use=2, weak=1; intrusive pointee has refcount 3):
- unique: `Point(3,-4)`; null: `nullptr`; fault: `<fault>`
- shared: `shared_ptr(Point(3,-4), use_count=2, weak_count=1)`; null: `shared_ptr(nullptr)`; counts print `-1` when the control block is null/unreadable
- intrusive: `intrusive_ptr(Point(9,3), ref_count=3)` (refcount read from the pointee at `ref_count_offset`); null: `intrusive_ptr(nullptr)`
- `remote_unique_ptr_view<Point, uint32_t>` for 32-bit target pointers works the same.

**Host-side field holders** `remote_unique_ptr<T,RP>`, `remote_shared_ptr<T,RP,RRC>`, `remote_intrusive_ptr<T,RefCountOffset,RP,RRC>`: standalone they print the raw address, e.g. `remote_unique_ptr<Point>(0x1100)` → `0x1100`, empty → `nullptr`, `remote_shared_ptr<Point>()` → `shared_ptr(nullptr)`, `remote_intrusive_ptr<Point,4>(0x1560)` → `0x1560`, empty → `intrusive_ptr(nullptr)`. Inside a `MICROFMT_REMOTE_STRUCT` field they are dereferenced like the views.

### `remote_object_view` (remote_object.hpp)
Register a struct, then print it from remote memory:

```cpp
struct Rec { uint32_t id; microfmt::string_ptr name; microfmt::remote_unique_ptr<Point> owner; microfmt::string32_ptr tag; };
MICROFMT_REMOTE_STRUCT_BEGIN(Rec)
MICROFMT_REMOTE_FIELD(id, uint32_t)
MICROFMT_REMOTE_FIELD(name, microfmt::string_ptr)
MICROFMT_REMOTE_FIELD(owner, microfmt::remote_unique_ptr<Point>)
MICROFMT_REMOTE_FIELD(tag, microfmt::string32_ptr)
MICROFMT_REMOTE_STRUCT_END()
alignas(8) std::byte sc[128];
println(out, "{}", remote_object_view(0x1600, sp, type_tag<Rec>{}, sc));
```
**Ctors:** `(addr, space, type_tag<T>{}, span<std::byte> scratch)` and `(addr, space, span<const remote_field_desc>, struct_size, struct_align, scratch)`. No spec.
Output (guest: id 77, name→"hello world", owner→0x1100, tag 0x1000; `owner` uses whatever formatter `Point` has, here assumed to print `Point(x,y)`; a `uint32_t` pointee prints as `unique: 11`-style plain value):
`{ id: 77, name: hello world, owner: Point(3,-4), tag: hello world }`
Notes: smart-pointer fields (`remote_unique_ptr`, `remote_shared_ptr`, `remote_intrusive_ptr`) render the pointee read from the target address space. null → `(null)`; unmapped → `<fault@0x9000>`; scratch smaller than the struct → `<fault@0x1600>`; scratch with no room left over for the string work buffer → string fields print `<invalid-foreign@0x1000>`. A vector of registered structs works: `{{ id: 77, ... }}`. Scratch must hold the struct plus a work buffer for strings.

### `memory_diff_view` (memory_diff.hpp)
`mem_diff<T1,T2>(span<const T1>, span<const T2>, uintptr_t base = 0)` (also byte-span / `std::span` overloads). Fields: `old_data`, `new_data`, `base_address`, `bytes_per_row = 16` (settable). No spec.

```cpp
auto d = mem_diff<uint8_t, uint8_t>(sa, sb, 0x20000000);   // 40 bytes; b[17]=0xff, b[33]=0xab
println(out, "{}", d);
```

```
  [... 1 identical rows hidden ...]
  0x20000010: 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F  -> 10 FF 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 
  0x20000020: 20 21 22 23 24 25 26 27  -> 20 AB 22 23 24 25 26 27 
```
With `d.bytes_per_row = 8` there are one-row-wide groups (`[... 2 identical rows hidden ...]` etc.). Identical data → `  [... 3 identical rows hidden ...]`. Only the smaller of the two sizes is compared; hex is uppercase.

### `remote_memory_diff_view` (remote_memory_diff.hpp)
`remote_mem_diff(address_space_ref old_space, uintptr_t old_addr, address_space_ref new_space, uintptr_t new_addr, size_t size, span<std::byte> scratch)`. No spec. Same row format; the address column is `new_addr + offset` (8 hex digits).
`remote_mem_diff(sp, 0x1700, sp, 0x1740, 40, rs)` with `std::byte rs[32]`:

```
  [... 1 identical rows hidden ...]
  0x00001750: 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F  -> 10 FF 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 
  0x00001760: 20 21 22 23 24 25 26 27  -> 20 AB 22 23 24 25 26 27 
```
Scratch smaller than `2*bytes_per_row` → `[ERROR: Scratch buffer too small for remote_memory_diff]`. Unreadable rows → `  0x00001800: [REMOTE READ FAULT]`.

### Symbol resolution: `remote_symbol_view`, `remote_fn_ptr`, `demangle_view`, `remote_diag_ref`
**Setup** (see `examples/unwinder_demo.cpp`): implement `symbol_resolver_traits<Tag>` with `using context_type = void;` and `static bool resolve(uintptr_t addr, span<char> scratch, raw_resolved_symbol& out)` (fields `symbol_name`, `symbol_base`, `image_name`, `image_load_base`, `is_exact`); handle: `symbol_resolver_ref resolver = symbol_resolver_ref::make<Tag>();` and `char scratch[64]; symbol_resolution_context ctx{scratch};`.

`remote_symbol_view(uintptr_t addr, symbol_resolver_ref, symbol_resolution_context&, bool demangle = true)` / `make_remote_symbol(...)`. **Spec:** a leading `#` = verbose (prefixes `image!`).
- `{}` → `core::database::query(char const*)+0x44`; `{:#}` → `user_app!core::database::query(char const*)+0x44`; `demangle=false` → `_ZN4core8database5queryEPKc+0x44`; exact hit → `main` / `user_app!main`; unknown → `<unknown@0x1234>`; null → `(null)`; no resolver → `0x1234`.

`remote_fn_ptr(uintptr_t, symbol_resolver_ref, symbol_resolution_context&)`. **Spec:** first char selects the mode: none = `symbol+0xoff` (demangled); `#` = `image!symbol+0xoff`; `x` or `p` = raw `0x401044`.
- `{}` → `core::database::query(char const*)+0x44`; `{:#}` → `user_app!core::database::query(char const*)+0x44`; `{:x}`/`{:p}` → `0x401044`; null → `(null)`; unresolved → `<unknown@0x1234>`; no resolver → `0x401044`.

`as_demangled(string_view)` (demangle.hpp), no spec — Itanium ABI demangler: `_ZN4core8database5queryEPKc` → `core::database::query(char const*)`; `_Z3fooi` → `foo(int)`; `main` → `main`; `_ZNSt6vectorIiSaIiEE9push_backERKi` → `std::vector<int, std::allocator<int>>::push_back(int const&)`; invalid `_Zgarbage` → printed unchanged; empty → empty.

`remote_diag_ref<T>(uintptr_t, address_space_ref, symbol_resolver_ref, span<std::byte> scratch, symbol_resolution_context&)`: forwards the spec to `formatter<T>`; null → `(null)`; read failure prints a fault note: `<fault:0x1234>` (plain address), `<fault:nullptr>`, `<fault@image!sym+0xoff>` when the failing address resolves (e.g. `<fault@user_app!core::database::query(char const*)+0x44>`). `format_remote_fault(sink, addr, resolver, ctx)` is the helper.

### Trap / backtrace views (exception_frame.hpp, frame_pointer.hpp, hybrid_unwinder.hpp)
All take a leading `#` spec for verbose symbols. Full setup is in `examples/unwinder_demo.cpp` (`frame_unwinder_traits`, `exception_frame_traits<Tag>`, `exception_matcher_traits<Tag>`, `make_register_context_ref`, `hybrid_stack_unwinder`).

`remote_trap_view(const trap_context&, exception_frame_ref decoder, symbol_resolver_ref, symbol_resolution_context&)`:

```
[Trap Level 0 @ 0xffff800000200000] Mode: Kernel
  Reason : Page Fault (#PF) (0xe)
  PC     : page_fault_handler+0x20
  SP     : 0xffff800000300000
  FP     : 0xffff800000300010
  LR     : main+0x30
```
`{:#}` makes `PC` print `vmlinux!page_fault_handler+0x20`. `LR` is only printed when non-zero; an undescribed vector prints `  Vector : 0x99` instead of `Reason`; vector 0 prints `Reason : Divide Error (#DE) (0x0)`.

`remote_backtrace_view(frame_pointer_iterator, resolver, ctx, uint32_t max_depth = 16)`; `frame_pointer_iterator(frame_unwinder_ref, register_context_ref, uintptr_t fp, uintptr_t pc)`. Line format `  #{:<2} fp={:#x}  pc=<sym>`:

```
  #0 fp=0x7ffca62a5cf0  pc=page_fault_handler+0x20
  #1 fp=0x7ffca62a5d70  pc=asm_exc_page_fault+0x8
```
`{:#}` with `max_depth=1` → `  #0 fp=0x7ffca62a5cf0  pc=vmlinux!page_fault_handler+0x20` (fp values vary per run).

`hybrid_backtrace_view(hybrid_stack_unwinder&, resolver, ctx, max_depth = 32)`: same lines plus a boundary marker at trap transitions:

```
  #0 fp=0x7ffca62a5cf0  pc=page_fault_handler+0x20
  #1 fp=0x7ffca62a5d70  pc=asm_exc_page_fault+0x8
  --- [Exception Boundary: User -> Kernel (vec 0xe)] ---
  #2 fp=0x7ffca62a6170  pc=core::database::query(char const*)+0x44
  #3 fp=0x7ffca62a6270  pc=main+0x30
```
(`Kernel -> User` / `Nested Fault (vec 0x..)` variants exist.)

### `register_context_view<Abi>` (register_view.hpp)
`register_context_view<Abi>(register_context_ref)` with `Abi` ∈ `x86_64_abi_traits`, `x86_abi_traits`, `aarch64_abi_traits`, `arm_abi_traits`, `riscv32_abi_traits`, `riscv64_abi_traits`. No spec. Build the context with `make_read_only_register_context_ref<read_fn>(state, space, std::byte scratch[8])`, where `read_fn(const void* state, address_space_ref, uint32_t dwarf_idx, void* out, size_t size) noexcept -> bool`.
- 64-bit ABIs: `NAME=0x%016x`, 2 per row; 32-bit ABIs: `NAME=0x%08x`, 3 per row; items separated by two spaces, rows by `\n`; only readable registers appear; null context → `<null register context>`.
- x86_64 (regs 0-3 readable) → `RAX=0x0000000000001000  RCX=0x0000000000001002` / `RDX=0x0000000000001001  RBX=0x0000000000001003`; arm →

```
R0=0x00000010  R1=0x00000011  R2=0x00000012
R3=0x00000013  R4=0x00000014  R5=0x00000015
...
PC=0x0000001f
```

### `metadata_map` (metadata_map.hpp / concrete_metadata_map.hpp)
Formatter prints JSON-like `{"key": value, ...}` (string values are not quoted); no spec; `null` if a value has no pointer. Build with `property_entry e[N]; concrete_metadata_map m(e); m.set("k", value_ref(v))` (borrows the value; returns `false` when full, updates an existing key), `m.make_view(span_iteration_state&)`. `md::write_metadata_table(writer&, metadata_map, span<char> scratch, title)` writes a markdown table.

```cpp
set: true true true      // a, s, ok
overflow: false  update: true
meta: {"ok": true, "s": x, "a": 9}      // iteration order is reverse insertion
```
Table (`write_metadata_table`): `### Meta`, then `| Property | Value |` rows (`| ok | true |`, `| s | x |`, `| a | 9 |`).

### `task_state`, `thread_state` (task.hpp, thread.hpp)
Upper-case names, no spec: `task_state` → `READY RUNNING SUSPENDED WAITING TERMINATED`; `thread_state` → `READY RUNNING SUSPENDED WAITING SLEEPING TERMINATED`.

---

## include/microfmt/markdown.hpp (`microfmt::md`)

**Include:** `<microfmt/markdown.hpp>`. Streaming Markdown writer; no allocation.

### Inline formatters (no spec; the views borrow the strings)
`bold(sv)` → `**x**`; `italic` → `_x_`; `strike` → `~~x~~`; `code` → `` `x` ``; `link(label, url)` → `[label](url)`; `image(alt, url)` → `![alt](url)`.

### `writer(const sink&)`
Chainable (`&`) methods: `write`, `put`, `newline`, `pad`, `print(fmt,...)`, `println(fmt,...)`, `heading(level, fmt, ...)`, `h1(title)`–`h4(title)` (plain string title, no args), `horizontal_rule`, `code_block_begin/end`, `code_block(lang, callable(writer&))`, `blockquote(fmt, ...)`, `list_item(fmt,...)`, `numbered_item(n, fmt,...)`, `task_item(bool, fmt,...)`, `nested_list_item(level, fmt,...)`, `alert(admonition, callable(writer&))` (callable writes `> ` lines itself), `table_header(span<const column>)`, `table_row_begin/table_cell(sv, column)/table_cell_fmt<Scratch=128>(column, fmt,...)/table_row_end`, `hexdump_block(summary, span<const uint8_t>)`, `get_sink()`.
Free: `details(writer&, summary, callable(writer&), bool open = false)`; RAII `details_guard(writer&, summary, open)`; `table_writer<N>(writer&, span<const column>)` (constructor emits the header; `.row(cells...)` prints a row; each cell uses the column width).
Types: `column{title, min_width, align}`, `align::{left, center, right}`, `admonition::{note, tip, important, warning, caution}`.

**Example:**

```cpp
md::writer d(out);
d.h1("Title").heading(2, "Sub {}", 2).println("{} {} {} {} {} {}", md::bold("b"), md::italic("i"),
    md::strike("s"), md::code("c"), md::link("L","http://x"), md::image("alt","a.png"));
d.blockquote("q {}", 1).list_item("item {}", 1).numbered_item(2, "n").task_item(true, "done")
 .task_item(false, "todo").nested_list_item(1, "nested").horizontal_rule();
d.code_block("cpp", [](md::writer& w){ w.write("int x;\n"); });
d.alert(md::admonition::tip, [](md::writer& w){ w.write("> hint\n"); });
const md::column cols[] = {{"Name",6,md::align::left},{"Val",5,md::align::right},{"C",4,md::align::center}};
d.table_header(span(cols));
d.table_row_begin().table_cell("ab", cols[0]).table_cell_fmt(cols[1], "{}", 42).table_cell("x", cols[2]).table_row_end();
md::table_writer<3> tw(d, span(cols)); tw.row("r2", 7, "y");
md::details(d, "Sum", [](md::writer& w){ w.write("body\n"); });
d.hexdump_block("Dump", span<const uint8_t>(bytes, 4));
```
Output:

```
# Title
## Sub 2
**b** _i_ ~~s~~ `c` [L](http://x) ![alt](a.png)
> q 1
- item 1
2. n
- [x] done
- [ ] todo
  - nested
---
```cpp
int x;

```
> [!TIP]
> hint

| Name   | Val   | C    |
| :----- | ----: | :--: |
| ab     |    42 |  x   |
| Name   | Val   | C    |
| :----- | ----: | :--: |
| r2     |     7 |  y   |
<details>
<summary>Sum</summary>

body

</details>

<details>
<summary>Dump</summary>

```text
0000000000000000: 01 02 03 41                                       |...A            |

```

</details>
```
(The inner `cpp` fence shown above is literal output; the table printed second is `table_writer`'s own header.)

---

## include/microfmt/log/ (logging)

No `formatter<>` specializations live here; log calls use the normal microfmt format-string syntax.
- `microfmt::log::basic_logger<MaxSinks, BufSize>(name, sinks...)`; level methods `trace/debug/info/warn/error/critical(fmt, args...)`, `log(level, fmt, args...)`, `set_level`, `flush`. The format argument is either a runtime `microfmt::string_view` or a compile-time `MICROFMT_STRING("...")` (a `compile_string_holder`), both accepted by every level method.
- Source-location-aware macros (`<microfmt/log/macros.hpp>`, C++20): `MICROFMT_LOGGER_{TRACE,DEBUG,INFO,WARN,ERROR,CRITICAL}(logger, fmt, ...)` and `MICROFMT_LOG_*` (need `MICROFMT_DEFAULT_LOGGER` or `MICROFMT_ENABLE_DEFAULT_LOGGER`).
- Arguments may be any formattable value, including hexdump and the inspector views above.

```cpp
microfmt::log::stdout_color_sink<512> console;
microfmt::log::basic_logger<1, 512> logger("telemetry", console.as_sink());
logger.set_level(microfmt::log::level::trace);
logger.info("Reading: {}", sensor_reading{7, 24, 3200});
logger.warn("Battery voltage is 0x{:04x} mV", 3200);
```
Output (colour codes stripped; timestamp varies):

```
[15:28:40.141] [telemetry] [INFO ] Reading: sensor=7, temperature=24 C, voltage=3200 mV
[15:28:40.141] [telemetry] [WARN ] Battery voltage is 0x0c80 mV
```

---

## Quick index

| Want | Use | Spec |
|---|---|---|
| Duration with unit | `{}` of `std::chrono::duration` | `c` hides suffix |
| UTC timestamp | `system_clock::time_point` | `t` time only, `d` date only (not both) |
| Uptime | `steady_clock::time_point` | none |
| Calendar y/m/d/weekday/hh_mm_ss | `chrono_calendar.hpp` | `month`: `s` for name |
| Binary 64.64 time | any `{sec, frac}` struct | `m u n p` / `3 6 9 1`, `r` raw |
| 32.32 fixed time | `as_sbintime(i64)` | `m u n`, `r` |
| timespec / timeval | native struct | `m u n`, `r` |
| Auto-scaled value | `auto_si`, `auto_bytes`, `hertz` | `.D` (0-15) |
| Value + unit | `with_unit(v, "u")` | forwarded to value formatter |
| File:line (function) | `source_loc()` | `s` = no function |
| Remote C string | `remote_string_view` / `foreign_string_view` | none |
| Remote typed value | `remote_ref<T>` | forwarded to `T` |
| 32-bit remote ptr | `compat32_ptr`, `make_remote_ref32`, `make_remote_string32` | none |
| Remote std::string-like | `make_remote_basic_string_view` + layout | none |
| Remote containers | `make_remote_vector/carray/forward_list/hash_table/binary_tree` + `.view` | none (`container_options`) |
| Remote smart pointers | `remote_unique/shared/intrusive_ptr_view` | none |
| Remote struct | `MICROFMT_REMOTE_STRUCT` + `remote_object_view` | none |
| Memory diff | `mem_diff` / `remote_mem_diff` | none |
| Symbol / fn pointer | `make_remote_symbol`, `remote_fn_ptr` | `#` verbose; fn_ptr also `x`/`p` raw |
| Demangle | `as_demangled` | none |
| Trap / backtrace | `remote_trap_view`, `remote_backtrace_view`, `hybrid_backtrace_view` | `#` verbose |
| Registers | `register_context_view<Abi>` | none |
| Metadata / states | `concrete_metadata_map`, `task_state`, `thread_state` | none |
| Markdown | `md::writer`, `md::bold/italic/strike/code/link/image` | none |
| Logging | `basic_logger::info(fmt, ...)`, `MICROFMT_LOG_*` | normal format specs |
