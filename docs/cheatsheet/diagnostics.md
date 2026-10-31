<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Cheatsheet: diagnostics, registers, and protocols

Part of the [formatter cheatsheet](../cheatsheet.md). All outputs below were produced by compiling and running real code (g++ -std=c++20) unless marked "(output unverified)".
Conventions: `out` = any `microfmt::sink`; `println(...)`/`format_to(out, ...)`/`format<N>(...)` all accept these views. Every view is **non-owning** (borrows spans/strings/pointers); never format a view whose source has died. Format-spec chars are scanned **by `for (char c : spec)`** in most of these formatters: order is free, unknown chars are silently ignored, no width/fill/align handling at all (`{:>8}` is ignored/does nothing for these views).

---

## `pointer.hpp` — raw address and raw pointer-range views

### `raw_ptr_view` via `raw_ptr(...)` / `raw_ptr32(...)`
- **Include:** `<microfmt/formatters/pointer.hpp>`
- **Types / factories:**
  - `enum class ptr_width_mode : uint8_t { native=0, compat32, compat64 }` (native = 16 hex digits on 64-bit, 8 on 32-bit)
  - `struct raw_ptr_view { uintptr_t address{0}; bool is_null{false}; ptr_width_mode width_mode{native}; }`
  - `template<class T> constexpr raw_ptr_view raw_ptr(const T* ptr, ptr_width_mode mode = native) noexcept` (works for `const void*`, `char*` too)
  - `constexpr raw_ptr_view raw_ptr(std::nullptr_t, ptr_width_mode mode = native)`
  - `constexpr raw_ptr_view raw_ptr(uintptr_t addr, ptr_width_mode mode = native)` (addr 0 ⇒ null)
  - `template<class T> constexpr raw_ptr_view raw_ptr32(const T* ptr)`, `raw_ptr32(std::nullptr_t)`, `raw_ptr32(uintptr_t addr)` (= `compat32`)
  - There is **no** `raw_ptr64`; use `raw_ptr(p, ptr_width_mode::compat64)` or spec `64`.
- **Spec:** `{:[p|P|x|X|32|64|0N|z]...}` (any order, tokens may be combined, e.g. `{:P64}`, `{:016x}`, `{:32z}`)

| Token | Meaning |
|---|---|
| `p` (default) | lowercase hex digits with `0x` prefix |
| `P` | uppercase hex digits with `0X` prefix and uppercase digits |
| `x` | lowercase, **no prefix** |
| `X` | uppercase, **no prefix** |
| `32` | force compat32: 8 digits **and address masked to low 32 bits** (spec overrides view's mode) |
| `64` | force compat64: 16 digits |
| `0N` (`0` followed by digits, e.g. `08`, `016`) | explicit minimum digit width N (zero-padded); beats 32/64 digit width (32-bit masking still applies if mode is compat32) |
| `z` | null prints `0x0` instead of `(nil)` |
| anything else (`{:4x}` → the `4` is ignored; `{:0}` alone ignored) | silently ignored |

- **Example:**

```cpp
microfmt::println("[{}] [{:p}] [{:P}] [{:x}] [{:X}] [{}]", raw_ptr(uintptr_t{0xdeadbeef}), raw_ptr(uintptr_t{0xdeadbeef}),
    raw_ptr(uintptr_t{0xdeadbeef}), raw_ptr(uintptr_t{0xdeadbeef}), raw_ptr(uintptr_t{0xdeadbeef}), raw_ptr(nullptr));
// -> [0x00000000deadbeef] [0x00000000deadbeef] [0X00000000DEADBEEF] [00000000deadbeef] [00000000DEADBEEF] [(nil)]
microfmt::println("[{}] [{:64}] [{:32}] [{:08x}] [{:016p}] [{:z}] [{:x}]", raw_ptr32(uintptr_t{0xabc}),
    raw_ptr(uintptr_t{0xabc}), raw_ptr(uintptr_t{0x1'0000'0abc}), raw_ptr(uintptr_t{0xabc}), raw_ptr(uintptr_t{0xabc}),
    raw_ptr(nullptr), raw_ptr(nullptr));
// -> [0x00000abc] [0x0000000000000abc] [0x00000abc] [00000abc] [0x0000000000000abc] [0x0] [(nil)]
microfmt::println("[{:08X}] [{}]", raw_ptr(uintptr_t{0xabc}), raw_ptr(uintptr_t{0x1'0000'0abc}, ptr_width_mode::compat32));
// -> [00000ABC] [0x00000abc]
```
- **Notes:** null prints `(nil)` regardless of `p/x/32/64` unless `z` (then `0x0`; with `x`/`X` still `0x0`). Digit width of the null case is never applied. Typed pointers are reinterpret-cast to `uintptr_t` (no deref). `{:0}` / `{:3}` have no effect.

### `raw_range_view<T>` via `raw_range(...)`
- **Include:** `<microfmt/formatters/pointer.hpp>`
- **Types / factories:**
  - `template<class T> struct raw_range_view { const T* begin_ptr; const T* end_ptr; string_view separator{", "}; char open_delim{'['}; char close_delim{']'}; size_t size() const; }` (public members; `separator` can be reassigned; `open_delim/close_delim` fields exist but the formatter ignores them, it uses `b/c/n`)
  - `template<class T> constexpr raw_range_view<T> raw_range(const T* first, const T* last)`
  - `template<class T> constexpr raw_range_view<T> raw_range(const T* first, size_t count)` (null `first` ⇒ empty)
  - reversed/null range ⇒ size 0 ⇒ prints only delimiters
- **Spec:** `{:[b|c|n]<elem-spec>}` — **only the first char** is checked for `b/B` `[ ]`, `c/C` `{ }`, `n/N` no delimiters; the **rest (or the whole spec if first char isn't b/c/n) is forwarded to `formatter<T>`** for every element. Default `[ ]`. Caveat: `b`/`c`/`n` as first char are always consumed, so an element spec that starts with those letters can't be forwarded (use `{:bX}` e.g. with `b` explicitly).
- **Example:**

```cpp
int arr[] = {1, 2, 255, 16};
microfmt::println("{} {:b} {:c} {:n} {:x} {:cx} {:n#x} {:04}", raw_range(arr,4), raw_range(arr,4), raw_range(arr,4),
    raw_range(arr,4), raw_range(arr,4), raw_range(arr,4), raw_range(arr,4), raw_range(arr, arr+3));
// -> [1, 2, 255, 16] [1, 2, 255, 16] {1, 2, 255, 16} 1, 2, 255, 16 [1, 2, ff, 10] {1, 2, ff, 10} 0x1, 0x2, 0xff, 0x10 [0001, 0002, 0255]
auto rv = raw_range(arr, 4); rv.separator = ";";   // -> [1;2;255;16]
const char* names[] = {"a","b"};   // {} -> [a, b]   {:?} -> ["a", "b"]
raw_range(arr, size_t{0}) -> []     raw_range<int>(nullptr, size_t{3}) -> []
```
- **Notes:** `T` needs a `formatter<T>`; range lifetime is the caller's. Separator string_view is borrowed.

---

## `register.hpp` — static register descriptors (`.rodata`) decoded against a value

### `reg_field`, `reg_descriptor<N>`, `reg_view<N, UInt>`, `format_reg(...)`
- **Include:** `<microfmt/formatters/register.hpp>`
- **Types / factories:**
  - `struct reg_field { string_view name; uint8_t bit_offset{}; uint8_t bit_width{}; bool is_flag{false}; }` — aggregate ctor args **in order**: `{name, bit_offset, bit_width, is_flag}`. A field with `bit_width == 1` is treated as a flag even if `is_flag=false`.
  - `template<size_t N> struct reg_descriptor { string_view name; size_t byte_width{4}; reg_field fields[N]; }` (byte_width 1/2/4/8 → hex digits = 2×byte_width)
    - CTAD (flat list, **no** inner braces): `constexpr reg_descriptor d{"CTRL", 4, reg_field{"EN",0,1,true}, reg_field{"MODE",4,3}, ...};`
    - explicit: `reg_descriptor<1>{"R8", 1, {reg_field{"F",0,4}}}`
  - `template<size_t N, class UInt = uint32_t> struct reg_view { UInt value; const reg_descriptor<N>* desc; }`
  - `template<size_t N, class UInt> constexpr auto format_reg(UInt val, const reg_descriptor<N>& desc) noexcept` → `reg_view<N,UInt>` (UInt deduced from the value type: pass `uint8_t{}`, `uint16_t{}` etc. explicitly; a plain `int` literal makes UInt=int)
- **Spec:** `{:[s|S|n|N]...}` (order free, other chars ignored)

| Token | Meaning |
|---|---|
| none | detailed: every field printed, in brackets: `NAME=0x…`/`NAME=d`, flags as `NAME` or `!NAME` |
| `s`, `S` | short: skip fields whose value is 0 (cleared flags and zero multi-bit fields omitted) |
| `n`, `N` | naked: no `[ ]`; output becomes `NAME=0xVALUE ` + fields (note separator is a single space, trailing space remains if nothing printed) |

- **Output format:** `NAME=0x<UPPER-HEX byte_width*2 digits> [f1, f2, ...]`. Flag (width 1 or `is_flag`): set → `NAME`, clear → `!NAME`. Multi-bit: value ≤ 9 → decimal (`MODE=3`), > 9 → `0x` + lowercase hex (`DIV=0xa`). Fields separated by `, `. Null `desc` ⇒ prints nothing. `bit_width==64` handled.
- **Example:**

```cpp
constexpr reg_descriptor ctrl{"CTRL", 4, reg_field{"EN",0,1,true}, reg_field{"MODE",4,3}, reg_field{"DIV",8,8},
                              reg_field{"RDY",1,1}, reg_field{"WIDE",16,1,true}};
println("{}",   format_reg(uint32_t{0x0A32}, ctrl)); // -> CTRL=0x00000A32 [!EN, MODE=3, DIV=0xa, RDY, !WIDE]
println("{:s}", format_reg(uint32_t{0x0A32}, ctrl)); // -> CTRL=0x00000A32 [MODE=3, DIV=0xa, RDY]
println("{:n}", format_reg(uint32_t{0x0A32}, ctrl)); // -> CTRL=0x00000A32 !EN, MODE=3, DIV=0xa, RDY, !WIDE
println("{:sn}", format_reg(uint32_t{0}, ctrl));     // -> "CTRL=0x00000000 " (trailing space)
format_reg(uint8_t{0x12},  reg_descriptor<1>{"R8", 1, {reg_field{"F",0,4}}});  // -> R8=0x12 [F=2]
format_reg(uint16_t{0x12}, reg_descriptor<1>{"R16",2, {reg_field{"F",0,4}}});  // -> R16=0x0012 [F=2]
format_reg(uint64_t{0x1234}, reg_descriptor<1>{"R64",8,{reg_field{"F",0,64}}}); // -> R64=0x0000000000001234 [F=0x1234]
```
- **Notes:** `reg_view` stores a **pointer** to the descriptor — keep it alive (a `constexpr`/static one is ideal; the temporary descriptors above are only OK because they're used within the same full-expression). No bounds check on bit_offset (≥64 is UB-ish shift).

---

## `bitfield.hpp` — named bit flags / multi-bit values for a `uint32_t`

### `bitfield_view`, `bits(...)`, `bit_field`, macros
- **Include:** `<microfmt/formatters/bitfield.hpp>`
- **Types / factories:**
  - `enum class bit_type : uint8_t { flag, value_hex, value_dec }`
  - `struct bit_field { uint32_t mask{}; uint8_t shift{0}; string_view name{}; bit_type type{flag}; }` — aggregate order `{mask, shift, name, type}`
  - `struct bitfield_view { uint32_t raw_value{0}; span<const bit_field> fields{}; string_view separator{" | "}; bool show_raw_hex{true}; }`
  - `constexpr bitfield_view bits(uint32_t raw_val, span<const bit_field> fields, bool show_raw_hex = true, string_view sep = " | ")`
  - Macros (**argument order matters**):
    - `MICROFMT_BIT_FLAG(pos, name)` → `{1u<<pos, pos, name, flag}`
    - `MICROFMT_BIT_VALUE_DEC(mask, shift, name)` → decimal value field
    - `MICROFMT_BIT_VALUE_HEX(mask, shift, name)` → `0x` lowercase-hex value field
  - `MICROFMT_DEFINE_REGISTER_TYPE(TypeName, UnderlyingType, field, field, ...)` — must be used at **global/enclosing-namespace scope outside `microfmt`** (it emits `template<> struct microfmt::formatter<TypeName>`); defines `struct TypeName { UnderlyingType value{0}; constexpr TypeName(); constexpr TypeName(UnderlyingType v); explicit constexpr operator UnderlyingType() const; static constexpr bit_field fields[] = {...}; }` plus a spec-less formatter. Value is cast to `uint32_t` (so ≤32-bit registers only). Always uses default separator and shows raw hex.
- **Spec:** no spec accepted (formatter ignores it; use `bits(...)` params for raw-hex/separator).
- **Output format:** `0x%08x [A | B | C=3 | D=0x12]`; with `show_raw_hex=false`: `[...]`. A `flag` field prints its name only when **all** mask bits are set; a value field prints `NAME=val` only when the extracted value is non-zero; mask 0 fields are skipped; nothing active ⇒ `NONE`.
- **Example:**

```cpp
constexpr bit_field fields[] = {MICROFMT_BIT_FLAG(0,"EN"), MICROFMT_BIT_FLAG(1,"RDY"),
    MICROFMT_BIT_VALUE_DEC(0xF0,4,"DIV"), MICROFMT_BIT_VALUE_HEX(0xFF00,8,"MODE"),
    MICROFMT_BIT_VALUE_HEX(0,0,"SKIP"), bit_field{0x30000,16,"EXACT",bit_type::flag}};
println("{}", bits(0x1235u, fields));                 // -> 0x00001235 [EN | DIV=3 | MODE=0x12]
println("{}", bits(0u, fields));                      // -> 0x00000000 [NONE]
println("{}", bits(0x2AB03u, fields, false));         // -> [EN | RDY | MODE=0xab]
println("{}", bits(0xA3u, fields, true, ", "));       // -> 0x000000a3 [EN, RDY, DIV=10]
println("{}", bits(0x10000u, fields));                // -> 0x00010000 [NONE]   (EXACT needs both mask bits)
println("{}", bits(0x30000u, fields));                // -> 0x00030000 [EXACT]

MICROFMT_DEFINE_REGISTER_TYPE(StatusReg, uint32_t, MICROFMT_BIT_FLAG(0,"EN"), MICROFMT_BIT_FLAG(1,"RDY"),
    MICROFMT_BIT_VALUE_DEC(0xF0,4,"DIV"), MICROFMT_BIT_VALUE_HEX(0xFF00,8,"MODE"))
println("{}", StatusReg{0x1A33});  // -> 0x00001a33 [EN | RDY | DIV=3 | MODE=0x1a]
println("{}", StatusReg{});        // -> 0x00000000 [NONE]
```
- **Notes:** `bitfield_view` borrows the field array and separator; keep fields `constexpr`/static. `MICROFMT_BIT_FLAG(pos,…)` with pos ≥ 32 overflows. Raw hex is lowercase here (contrast `reg_view`, which is uppercase).

---

## `hexdump.hpp` — `xxd`-like memory dump (direct or fault-checked)

### `hexdump_view` via `hexdump(...)`, `hexdump_checked(...)`, `hexdump_to(...)`, `format_hexdump(...)`
- **Include:** `<microfmt/formatters/hexdump.hpp>`
- **Types / factories:**
  - `using memory_reader_fn_t = size_t (*)(void* ctx, uintptr_t src_addr, uint8_t* dst_buf, size_t max_len) noexcept;` (returns bytes actually read; fewer than requested ⇒ rest shown as `??`)
  - `struct hexdump_view { uintptr_t base_addr{0}; span<const uint8_t> direct_data{}; void* reader_ctx{nullptr}; memory_reader_fn_t reader_fn{nullptr}; size_t total_len{0}; uint8_t bytes_per_line{16}; bool show_ascii{true}; bool show_address{true}; bool uppercase{false}; }` (public; `show_address`/`uppercase` only settable by mutating the view)
  - `constexpr hexdump_view hexdump(span<const uint8_t> data, uintptr_t base_addr = 0, uint8_t bytes_per_line = 16, bool show_ascii = true)` (0 bpl → 16)
  - `constexpr hexdump_view hexdump_checked(uintptr_t base_addr, size_t total_len, memory_reader_fn_t reader, void* ctx = nullptr, uint8_t bytes_per_line = 16, bool show_ascii = true)`
  - `inline void hexdump_to(const sink& out, span<const uint8_t> data, uintptr_t base_address = 0)` (defaults: 16 B/line, ASCII on)
  - `void format_hexdump(const hexdump_view& h, span<uint8_t> line_buffer, const sink& out) noexcept` — caller-owned line storage; effective bytes/line = min(requested, 32, line_buffer.size()).
- **Spec:** no spec accepted. Config only through factory arguments / view fields.
- **Output format (per line, always `\n`-terminated, including the last):** `AAAAAAAAAAAAAAAA: hh hh … hh  hh … |ascii|` — address = 16 hex digits on 64-bit (8 on 32-bit), then `: `; each byte `hh ` ; an extra space after the 8th byte when bytes/line > 8; short last line padded with spaces; ASCII pane ` |…|` with non-printables as `.` (unread bytes as `?`, `??` in hex).
- **Example:**

```cpp
const uint8_t data[] = {0x48,0x65,0x6c,0x6c,0x6f,0x00,0x7f,0xff,0x10,0x20,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48};
println("{}", hexdump(data, 0x1000));
// 0000000000001000: 48 65 6c 6c 6f 00 7f ff  10 20 41 42 43 44 45 46  |Hello.... ABCDEF|
// 0000000000001010: 47 48                                             |GH              |
println("{}", hexdump(data, 0x20, 8, false));
// 0000000000000020: 48 65 6c 6c 6f 00 7f ff 
// 0000000000000028: 10 20 41 42 43 44 45 46 
// 0000000000000030: 47 48                   
println("{}", hexdump(span<const uint8_t>(data, 6), 0, 4));
// 0000000000000000: 48 65 6c 6c  |Hell|
// 0000000000000004: 6f 00        |o.  |
size_t rd(void*, uintptr_t a, uint8_t* dst, size_t n) noexcept { /* reads only addresses < 0x1008 */ }
println("{}", hexdump_checked(0x1000, 16, rd, nullptr, 8));
// 0000000000001000: 41 42 43 44 45 46 48 ...  |ABCDEFGH|
// 0000000000001008: ?? ?? ?? ?? ?? ?? ?? ??  |????????|
auto h = hexdump(span<const uint8_t>(data,3), 0xABC); h.uppercase = true; h.show_address = false;
println("{}", h);   // -> 48 65 6C                                          |Hel             |
hexdump_to(stdout_sink(), span<const uint8_t>(data,4), 0x80);  // -> 0000000000000080: 48 65 6c 6c      ... |Hell            |
```
(Empty/zero-length ⇒ prints nothing; `println("{}", ...)` shows the trailing blank line.)
- **Notes:** `formatter<hexdump_view>` uses a 32-byte stack line buffer; bytes_per_line > 32 is clamped to 32 (but padding computed for the clamped value). `hexdump()` borrows the span; `hexdump_checked` borrows `ctx`. Reader is invoked per line with `(ctx, current_addr, buf, chunk_len)`; it must not throw (noexcept).

---

## `net.hpp` — MAC / EUI addresses

### `mac_view` via `mac(...)`
- **Include:** `<microfmt/formatters/net.hpp>`
- **Types / factories:** `struct mac_view { span<const uint8_t> bytes; char separator{':'}; bool uppercase{false}; }`
  - `mac(span<const uint8_t> bytes, char separator=':', bool uppercase=false)`
  - `mac(const uint8_t (&arr)[6], char sep=':', bool uppercase=false)`, `mac(const uint8_t (&arr)[8], …)`
  - `template<class C> mac(const C& c, char sep=':', bool uppercase=false)` for any container with `.data()` (convertible to `const uint8_t*`) and `.size()` (e.g. `std::array<uint8_t,6>`, `reloco::array`); **rvalue containers are `= delete`d**.
  - Only 6-byte (MAC-48) or 8-byte (EUI-64) inputs print the address; any other size prints `00:00:00:00:00:00` (always 17 chars, always `:`).
- **Spec:** `{:[x|X|-|:|.|_]...}`

| Token | Meaning |
|---|---|
| `x` | force lowercase (overrides view's `uppercase`) |
| `X` | force uppercase (overrides view) |
| `:` `-` `.` `_` | separator (overrides view's `separator`; last one wins) |
| none | uses the factory's `separator`/`uppercase` (default `:` lowercase) |

An empty separator can only come from the factory (`mac(b, '\0')`) and cannot be restored via spec.
- **Example:**

```cpp
const uint8_t m6[6] = {0xde,0xad,0xbe,0xef,0x00,0x1a};
println("{} {:X} {:-} {:X-} {:.} {:_}", mac(m6), mac(m6), mac(m6), mac(m6), mac(m6), mac(m6));
// -> de:ad:be:ef:00:1a DE:AD:BE:EF:00:1A de-ad-be-ef-00-1a DE-AD-BE-EF-00-1A de.ad.be.ef.00.1a de_ad_be_ef_00_1a
mac(m8, '-', true)               // EUI-64 -> DE-AD-BE-EF-00-1A-2B-3C
mac(span<const uint8_t>(m5, 5))  // -> 00:00:00:00:00:00
mac(m6, '\0') -> deadbeef001a     mac(m6, ' ') -> de ad be ef 00 1a     mac(std::array<uint8_t,6>{1,2,3,4,5,6}) -> 01:02:03:04:05:06
```
- **Notes:** the view borrows the bytes (don't pass temporaries; the rvalue overload is deleted for containers).

---

## `semver.hpp` — semantic versions

### `semver` via `version(...)`, `from_packed32(...)`, `from_packed24(...)`
- **Include:** `<microfmt/formatters/semver.hpp>`
- **Types / factories:**
  - `struct semver { uint16_t major{0}, minor{0}, patch{0}; string_view prerelease{}; string_view build{}; bool show_v_prefix{false}; }`
  - `constexpr semver version(uint16_t major, uint16_t minor, uint16_t patch, string_view prerelease = "", string_view build = "", bool show_v = false)`
  - `constexpr semver from_packed32(uint32_t packed, bool show_v = false)` — layout `major=bits31..24, minor=bits23..16, patch=bits15..0` (8-8-16)
  - `constexpr semver from_packed24(uint32_t packed, bool show_v = false)` — `major=bits23..16, minor=15..8, patch=7..0` (8-8-8)
- **Spec:** `{:[#|v|V|c|C]...}`

| Token | Meaning |
|---|---|
| `#`, `v`, `V` | force leading `v` (also when view `show_v_prefix` is false; cannot remove a `v` set by the view) |
| `c`, `C` | core only: hide `-prerelease` and `+build` |
| none | `[v]MAJOR.MINOR.PATCH[-pre][+build]` |

- **Example:**

```cpp
auto v = version(1,2,3,"rc.1","b42");
println("{} {:v} {:#} {:c} {:cv}", v, v, version(1,2,3), v, v);
// -> 1.2.3-rc.1+b42 v1.2.3-rc.1+b42 v1.2.3 1.2.3 v1.2.3
println("{} {} {} {}", version(1,2,3,"","b42",true), version(1,2,3,"alpha"), from_packed32(0x01020003), from_packed24(0x010203,true));
// -> v1.2.3+b42 1.2.3-alpha 1.2.3 v1.2.3
from_packed32(0x0102FFFF) -> 1.2.65535
```
- **Notes:** `prerelease`/`build` are borrowed `string_view`s (use literals or outlive the format call). Not validated/parsed (despite the file comment "parsing"). Registered type-id name `microfmt::semver`.

---

## `uuid.hpp` — UUID views (+ optional Boost.UUID)

### `uuid_view` via `uuid(...)`
- **Include:** `<microfmt/formatters/uuid.hpp>`
- **Types / factories:** `struct uuid_view { span<const uint8_t> bytes{}; bool uppercase{false}; bool braced{false}; }`
  - `uuid(span<const uint8_t> b, bool uppercase=false, bool braced=false)`
  - `uuid(const uint8_t (&arr)[16], bool uppercase=false, bool braced=false)`
  - `template<class C> uuid(const C& arr, …)` for any container with `.data()`/`.size()` (e.g. `std::array<uint8_t,16>`); rvalue containers `= delete`d
  - `uuid(const boost::uuids::uuid& u, bool uppercase=false, bool braced=false)` (only if `MICROFMT_HAS_BOOST_UUID`; rvalue deleted)
  - fewer than 16 bytes ⇒ prints `00000000-0000-0000-0000-000000000000` (never braced); extra bytes beyond 16 are ignored.
- **Spec:** `{:[X|#]...}`

| Token | Meaning |
|---|---|
| `X` | uppercase hex |
| `#` | wrap in `{ }` |
| `x` / other | ignored (**cannot** lower-case a view created with `uppercase=true`; spec only ORs with the view's flags) |
| none | lowercase canonical `8-4-4-4-12` |

- **Example:**

```cpp
const uint8_t u[16] = {0x6b,0xa7,0xb8,0x10,0x9d,0xad,0x11,0xd1,0x80,0xb4,0x00,0xc0,0x4f,0xd4,0x30,0xc8};
println("{} {:X} {:#} {:#X} {}", uuid(u), uuid(u), uuid(u), uuid(u), uuid(u,true,true));
// -> 6ba7b810-9dad-11d1-80b4-00c04fd430c8 6BA7B810-9DAD-11D1-80B4-00C04FD430C8 {6ba7b810-…c8} {6BA7B810-…C8} {6BA7B810-9DAD-11D1-80B4-00C04FD430C8}
uuid(span<const uint8_t>(u,15)) -> 00000000-0000-0000-0000-000000000000
std::array<uint8_t,16> a{}; a[0]=0xab; uuid(a) -> ab000000-0000-0000-0000-000000000000
```
(Full lines were verified; `…` abbreviates identical digits.)

### `formatter<boost::uuids::uuid>` (Boost.UUID, opt-in)
- **Gate:** define `MICROFMT_ENABLE_BOOST_UUID` (or include `<boost/uuid/uuid.hpp>` first) **before** the microfmt header; otherwise the Boost overloads/formatter don't exist. (Compile error `#error` if enabled but boost headers missing.)
- **Spec:** same `{:[X|#]}`; default lowercase, no braces.
- **Example (verified):**

```cpp
#define MICROFMT_ENABLE_BOOST_UUID
boost::uuids::uuid id = boost::uuids::string_generator()("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
println("{} {:X} {:#} {:#X}", id, id, id, id);
// -> 6ba7b810-9dad-11d1-80b4-00c04fd430c8 6BA7B810-9DAD-11D1-80B4-00C04FD430C8 {6ba7b810-9dad-11d1-80b4-00c04fd430c8} {6BA7B810-9DAD-11D1-80B4-00C04FD430C8}
println("{} {}", uuid(id), uuid(id,true,true));   // -> 6ba7b810-… {6BA7B810-9DAD-11D1-80B4-00C04FD430C8}
```
- **Notes:** `uuid_view`s borrow bytes. Registered type-id name `microfmt::uuid_view`.

---

## `variant.hpp` — `std::variant`, `std::monostate`, `variant_view`

### `formatter<std::variant<Ts...>>`, `variant_view<Ts...>`, `as_variant(...)`, `as_debug_variant(...)`
- **Include:** `<microfmt/formatters/variant.hpp>` (opt-in std interop). **Every alternative `Ts` must have a `formatter` (e.g. include `floating.hpp` for float/double)**, else a hard "incomplete type" error.
- **Types / factories:**
  - `variant_view<Ts...>` ctor `explicit (const std::variant<Ts...>& var, string_view prefix = "", string_view suffix = "", bool show_index = false)`; rvalue-variant ctors `= delete`; accessors `get()`, `prefix()`, `suffix()`, `show_index()`.
  - `as_variant(const std::variant<Ts...>& v)` → plain
  - `as_variant(const std::variant<Ts...>& v, string_view prefix, string_view suffix)` → decorated
  - `as_debug_variant(const std::variant<Ts...>& v, bool show_index = false)` → prefix `"variant"` (renders `variant(...)`, or `variant[i](...)` when `show_index`)
  - all take lvalues only (rvalue overloads `= delete`d).
- **Spec:** `{:[?|i|#]<elem-spec>}` — only the **first** char is checked for a mode; the remainder is forwarded to the active alternative's formatter.

| Mode (first char) | Output |
|---|---|
| none | active alternative only (prefix/suffix of `as_variant(v,pre,suf)` wrap it: `<hi>`) |
| `?` | `variant(<alt>)` (forces debug wrapper; drops custom prefix/suffix) |
| `i` or `#` | `variant[<index>](<alt>)` |
| otherwise | whole spec → element formatter (e.g. `{:x}`) |
| `as_debug_variant(v)` (no `show_index`) + no `i` | `variant(<alt>)`; with `{:#}` still `variant(...)` (only `i` or `show_index=true` gives index) |
| valueless_by_exception | `valueless` (all modes) |

- **Example:**

```cpp
std::variant<std::monostate,int,std::string_view,long> v = 42, vs = std::string_view("hi"), vm;
println("{} {} {}", v, vs, vm);        // -> 42 hi null
println("{:?} {:?} {:?}", v, vs, vm);  // -> variant(42) variant(hi) variant(null)
println("{:i} {:i} {:i}", v, vs, vm);  // -> variant[1](42) variant[2](hi) variant[0](null)
println("{:#} {:#}", v, vs);           // -> variant[1](42) variant[2](hi)
std::variant<int,unsigned> vh = 255;
println("{:x} {:i#x} {:?x} {:#x}", vh, vh, vh, vh);  // -> ff variant[0](0xff) variant(ff) variant[0](ff)
println("{} | {}", as_variant(v), as_variant(vs, "<", ">"));      // -> 42 | <hi>
println("{}", as_variant(vm, "<", ">"));                           // -> <null>
println("{} | {} | {}", as_debug_variant(v), as_debug_variant(v,true), as_debug_variant(vs)); // -> variant(42) | variant[1](42) | variant(hi)
println("{:i} {:?} {:#} {}", as_debug_variant(v), as_debug_variant(v), as_debug_variant(v), as_debug_variant(vm,true));
// -> variant[1](42) variant(42) variant(42) variant[0](null)
println("{:?} {:i}", as_variant(v,"<",">"), as_variant(v,"<",">"));  // -> variant(42) variant[1](42)
```
- **Notes:** the view borrows the variant (`value_ref`), so never `as_variant(make_variant())`. A leading `#` is consumed as the index mode, so `{:#x}` is "index + hex", not `0x` prefix — to get the `0x` prefix with an index use `{:i#x}`. Plain `{:?}` on a `std::variant` is this `?` mode (prints `variant(…)`). `std::string_view` alternatives work (formatter in `std_types.hpp`).

### `formatter<std::monostate>`
- Writes the literal `null`. No spec accepted (`parse` is a no-op). `println("{}", std::monostate{})` → `null`.

---

## `grid_view.hpp` — register block rendered as an aligned grid

### `reg_grid_desc<WordType, N>`, `reg_grid_view<WordType, N>`, `make_reg_grid(...)`
- **Include:** `<microfmt/formatters/grid_view.hpp>`
- **Types / factories:**
  - `template<class WordType, size_t N> struct reg_grid_desc { string_view title; uint8_t columns{4}; string_view names[N]; }` — aggregate init `{title, columns, {names...}}`; **explicit template args required** (`reg_grid_desc<uint32_t,5> gd{"GPIO", 2, {"MODE", …}}`): the shipped deduction guide takes a `reg_grid_type_tag<WordType>` as first parameter, which an aggregate init can't supply, so CTAD does not work in practice.
  - `template<class WordType, size_t N> struct reg_grid_view { const WordType* values; const reg_grid_desc<WordType,N>* desc; }`
  - `template<class StructOrArray, class WordType, size_t N> constexpr auto make_reg_grid(const StructOrArray& data, const reg_grid_desc<WordType,N>& desc)` — `static_assert(sizeof(data) >= N*sizeof(WordType))`; reinterpret-casts `&data` to `const WordType*` (strict-aliasing/padding responsibility on the caller; struct-of-registers is fine).
- **Spec:** no spec accepted (silently ignored).
- **Output format:** optional `=== title ===\n` (skipped when title empty), then cells `NAME` padded with spaces to 5 chars (longer names get no padding), `= 0x` + **uppercase** hex of `2*sizeof(WordType)` digits; cells separated by two spaces; newline after every `columns`-th cell and after the last one (columns==0 ⇒ 4). Null `values`/`desc` ⇒ nothing. Every call ends with `\n`.
- **Example:**

```cpp
constexpr reg_grid_desc<uint32_t,5> gd{"GPIO", 2, {"MODE","OTYPER","OSPEEDR","PUPDR","ID"}};
uint32_t regs[5] = {0x28000000, 0, 0xC000000, 0x64000000, 0xFF};
println("{}", make_reg_grid(regs, gd));
// === GPIO ===
// MODE = 0x28000000  OTYPER= 0x00000000
// OSPEEDR= 0x0C000000  PUPDR= 0x64000000
// ID   = 0x000000FF
// (+ blank line from println)
constexpr reg_grid_desc<uint16_t,3> g2{"", 4, {"A","B","C"}};  uint16_t r16[3] = {1,2,0xABCD};
// A    = 0x0001  B    = 0x0002  C    = 0xABCD
constexpr reg_grid_desc<uint8_t,2> g3{"T", 3, {"X","Y"}};   uint8_t r8[2] = {1,2};
// === T ===
// X    = 0x01  Y    = 0x02
```
- **Notes:** the view borrows both `data` and `desc`.

---

## `can.hpp` — CAN 2.0 / CAN-FD frames

### `can_frame_view` via `can_frame`, `can_extended`, `can_fd`
- **Include:** `<microfmt/formatters/can.hpp>`
- **Types / factories:**
  - `enum class can_flags : uint8_t { none=0, extended=1, rtr=2, fd=4, brs=8, esi=16 }` with `operator|` and `bool operator&(a,b)` (non-zero test)
  - `struct can_frame_view { uint32_t id{0}; span<const uint8_t> payload{}; can_flags flags{none}; }`
  - `constexpr can_frame_view can_frame(uint16_t id, span<const uint8_t> payload, bool rtr=false)` — id masked `& 0x7FF`
  - `template<size_t N> constexpr can_frame_view can_frame(uint16_t id, const uint8_t (&arr)[N], bool rtr=false)`
  - `constexpr can_frame_view can_extended(uint32_t id, span<const uint8_t> payload, bool rtr=false)` — id masked `& 0x1FFFFFFF`, flag `extended` (no array overload; arrays convert to span)
  - `constexpr can_frame_view can_fd(uint32_t id, span<const uint8_t> payload, bool is_extended=false, bool brs=false, bool esi=false)` — id masked by 29/11 bits; always `fd` flag
  - Payload length is not clamped (8/64 limits are the caller's job); RTR frames print DLC from the span size.
- **Spec:** `{:[c|C|x]...}`

| Token | Meaning |
|---|---|
| none | diagnostic: `CAN [0xID] [EXT] [BRS] [ESI] DLC=n DATA: HH HH …` (`CAN-FD` for fd; `RTR (DLC=n)` for RTR, no data) |
| `c`, `C` | candump style `ID#DATA`, FD `ID##<flags-nibble>DATA` (flags: BRS=1, ESI=2), RTR `ID#Rn` |
| `x` | lowercase hex (default **uppercase**) |
| `X`, others | ignored |

ID is zero-padded to 3 hex digits (standard) or 8 (extended). In candump mode an FD frame wins over RTR (RTR flag ignored); diagnostic mode on fd+rtr prints `CAN-FD … RTR (DLC=n)`. EXT is derived from the `extended` flag, even without fd.
- **Example:**

```cpp
const uint8_t p[] = {0xDE,0xAD,0xBE,0xEF,0x0a}, p8[] = {2,1,12,0,0,0,0,0};
can_frame(0x123, p)         // {}   -> CAN [0x123] DLC=5 DATA: DE AD BE EF 0A
                            // {:c} -> 123#DEADBEEF0A
can_frame(0x7ab, p)         // {:x} -> CAN [0x7ab] DLC=5 DATA: de ad be ef 0a    {:cx} -> 7ab#deadbeef0a   {:C} -> 7AB#DEADBEEF0A
can_frame(0xFFFF, p) -> CAN [0x7FF] …;  can_extended(0xFFFFFFFF, p) -> CAN [0x1FFFFFFF] EXT DLC=5 DATA: …
can_extended(0x18DAF110, p8) // {}   -> CAN [0x18DAF110] EXT DLC=8 DATA: 02 01 0C 00 00 00 00 00
                             // {:c} -> 18DAF110#02010C0000000000
can_frame(0x100, span<const uint8_t>(p,3), true)   // {} -> CAN [0x100] RTR (DLC=3)     {:c} -> 100#R3
can_extended(0x1ABCDE, span<const uint8_t>(), true)// {} -> CAN [0x001ABCDE] EXT RTR (DLC=0)  {:c} -> 001ABCDE#R0
can_frame(0x1, span<const uint8_t>())              // {} -> CAN [0x001] DLC=0      {:c} -> 001#
can_fd(0x123, p)                    // {} -> CAN-FD [0x123] DLC=5 DATA: DE AD BE EF 0A   {:c} -> 123##0DEADBEEF0A
can_fd(0x123, p, false, true, true) // {} -> CAN-FD [0x123] BRS ESI DLC=5 DATA: …        {:c} -> 123##3DEADBEEF0A
can_fd(0x18DAF110, p, true, true)   // {} -> CAN-FD [0x18DAF110] EXT BRS DLC=5 DATA: …
can_fd(0x18DAF110, p, true, false, true) // {:c} -> 18DAF110##2DEADBEEF0A
```
- **Notes:** borrows payload. `BRS`/`ESI` are printed only when `fd` is set.

---

## `i2c.hpp` — I2C transactions

### `i2c_msg_view` via `i2c_write`, `i2c_read`, `i2c_10bit`
- **Include:** `<microfmt/formatters/i2c.hpp>`
- **Types / factories:**
  - `enum class i2c_dir : uint8_t { write=0, read=1 }`; `enum class i2c_status : uint8_t { ok=0, nack_addr, nack_data, arb_lost, timeout }`; `enum class i2c_flags : uint8_t { none=0, ten_bit=1, nostart=2, stop=4 }` (`|`, `&` ops; only `ten_bit` affects output)
  - `struct i2c_msg_view { uint16_t addr{0}; i2c_dir direction; span<const uint8_t> payload{}; i2c_status status{ok}; i2c_flags flags{none}; }`
  - `i2c_write(uint8_t addr_7bit, span<const uint8_t> data, i2c_status status = ok)` and `template<size_t N> i2c_write(uint8_t, const uint8_t (&)[N], i2c_status = ok)` (addr masked `& 0x7F`)
  - `i2c_read(...)`: same two overloads
  - `i2c_10bit(uint16_t addr_10bit, i2c_dir dir, span<const uint8_t> data, i2c_status status = ok)` (addr masked `& 0x3FF`, sets `ten_bit`)
- **Spec:** `{:[c|C|x]...}`

| Token | Meaning |
|---|---|
| none | verbose: `I2C [0xAA] [10b] WR\|RD (n B) [DATA: HH …] -> STATUS` |
| `c`, `C` | compact: `0xAA:W[HH HH]` / `0xAA:R[…]`, plus `!(STATUS)` only when status ≠ ok |
| `x` | lowercase hex (default uppercase; address too); `X` ignored |

Address digits: 2 (7-bit) or 3 (10-bit). STATUS names: `OK NACK_ADDR NACK_DATA ARB_LOST TIMEOUT`.
- **Example:**

```cpp
const uint8_t p[] = {0xDE,0xAD,0xBE,0xEF,0x0a};
i2c_write(0x68, p)   // {} -> I2C [0x68] WR (5 B) DATA: DE AD BE EF 0A -> OK    {:c} -> 0x68:W[DE AD BE EF 0A]
i2c_read(0x68, span<const uint8_t>(p,2)) // {} -> I2C [0x68] RD (2 B) DATA: DE AD -> OK    {:c} -> 0x68:R[DE AD]
i2c_write(0x6a, p)   // {:x} -> I2C [0x6a] WR (5 B) DATA: de ad be ef 0a -> OK   {:cx} -> 0x6a:W[de ad be ef 0a]   {:C} -> 0x6A:W[DE AD BE EF 0A]
i2c_10bit(0x2AB, i2c_dir::read, span<const uint8_t>(p,2))  // {} -> I2C [0x2AB] 10b RD (2 B) DATA: DE AD -> OK
i2c_10bit(0x2AB, i2c_dir::write, span<const uint8_t>(p,2)) // {:c} -> 0x2AB:W[DE AD]
i2c_write(0x50, p, i2c_status::nack_addr) // {} -> I2C [0x50] WR (5 B) DATA: DE AD BE EF 0A -> NACK_ADDR   {:c} -> 0x50:W[DE AD BE EF 0A]!(NACK_ADDR)
i2c_read(1, span<const uint8_t>(), i2c_status::nack_data) // {} -> I2C [0x01] RD (0 B) -> NACK_DATA
i2c_write(0x1, span<const uint8_t>()) // {:c} -> 0x01:W[]       i2c_read(1, {}, timeout) {:c} -> 0x01:R[]!(TIMEOUT)
```
- **Notes:** the `i2c_10bit` data span has no array overload (arrays convert implicitly to span).

---

## `spi.hpp` — SPI transfers (MOSI/MISO)

### `spi_transfer_view` via `spi_duplex`, `spi_write`, `spi_read`
- **Include:** `<microfmt/formatters/spi.hpp>`
- **Types / factories:**
  - `enum class spi_mode : uint8_t { mode0, mode1, mode2, mode3 }`; `enum class spi_status : uint8_t { ok=0, timeout, crc_err, overrun }`
  - `struct spi_transfer_view { span<const uint8_t> mosi{}; span<const uint8_t> miso{}; uint8_t cs_pin{0}; spi_mode mode{mode0}; spi_status status{ok}; }`
  - `spi_duplex(span<const uint8_t> mosi, span<const uint8_t> miso, uint8_t cs=0, spi_mode mode=mode0, spi_status status=ok)` + `template<size_t N1,size_t N2>` array overload
  - `spi_write(span<const uint8_t> mosi, uint8_t cs=0, spi_mode=mode0, spi_status=ok)` + `template<size_t N>` array overload (MISO empty)
  - `spi_read(span<const uint8_t> miso, uint8_t cs=0, spi_mode=mode0, spi_status=ok)` + array overload (MOSI empty)
- **Spec:** `{:[c|C|x]...}`

| Token | Meaning |
|---|---|
| none | verbose: `SPI [CS<n>, Mode <m>] (<len> B) [MOSI: …] [\| MISO: …] -> STATUS` (len = max(mosi, miso sizes)) |
| `c`, `C` | compact: `CS<n>:TX[…]/RX[…]` (sections only if non-empty) + `!(STATUS)` if not ok |
| `x` | lowercase hex (default uppercase); `X` ignored |

Status names: `OK TIMEOUT CRC_ERR OVERRUN`. Empty verbose transfer: `SPI [CS0, Mode 0] (0 B) -> OK`; empty compact: `CS0:`.
- **Example:**

```cpp
const uint8_t tx[] = {0x9F,0,0}, rx[] = {0,0xEF,0x40};
spi_duplex(tx, rx)                       // {}  -> SPI [CS0, Mode 0] (3 B) MOSI: 9F 00 00 | MISO: 00 EF 40 -> OK
spi_duplex(tx, rx, 1, spi_mode::mode3)   // {:c} -> CS1:TX[9F 00 00]/RX[00 EF 40]
spi_duplex(tx, rx, 2, spi_mode::mode2, spi_status::crc_err) // {} -> SPI [CS2, Mode 2] (3 B) MOSI: 9F 00 00 | MISO: 00 EF 40 -> CRC_ERR
spi_write(tx)                            // {} -> SPI [CS0, Mode 0] (3 B) MOSI: 9F 00 00 -> OK     {:c} -> CS0:TX[9F 00 00]
spi_read(rx, 1, spi_mode::mode1, spi_status::timeout) // {} -> SPI [CS1, Mode 1] (3 B) MISO: 00 EF 40 -> TIMEOUT
spi_read(rx, 1, spi_mode::mode1, spi_status::overrun) // {:c} -> CS1:RX[00 EF 40]!(OVERRUN)
spi_duplex(tx, rx)  // {:x} -> SPI [CS0, Mode 0] (3 B) MOSI: 9f 00 00 | MISO: 00 ef 40 -> OK   {:cx} -> CS0:TX[9f 00 00]/RX[00 ef 40]
spi_duplex(span<const uint8_t>(tx,1), rx) // {} -> SPI [CS0, Mode 0] (3 B) MOSI: 9F | MISO: 00 EF 40 -> OK
```

---

## `json.hpp` — streaming JSON writer (`microfmt::json`, no allocation)

All writers take a `microfmt::sink` **by value** (copy of the handle; the target buffer must outlive them), emit text **immediately** (no buffering/validation), and **close themselves in the destructor** (`}` / `]`). `end()` closes early and is idempotent (`closed_` flag). Moves are allowed, copies deleted.

### `json::write_escaped_string(const sink&, string_view)`
Writes `"…"` escaping `" \\ \b \f \n \r \t` and other bytes < 0x20 as `\u00XX` (uppercase hex). Bytes ≥ 0x20 (incl. 0x7F and UTF-8) pass through unescaped. Keys are escaped too.

### `json::object_writer`
- **Include:** `<microfmt/formatters/json.hpp>`
- **Ctor:** `explicit object_writer(sink out)` — writes `{` immediately.
- **Members (all return `object_writer&` unless noted; commas handled automatically):**

| Method | Writes |
|---|---|
| `key(string_view k)` | `"k":` — no value-writing method exists afterwards, so a dangling key yields invalid JSON (e.g. `"raw":}`); prefer `kv`/`nested_*` |
| `kv(string_view k, string_view v)` | `"k":"v"` (escaped) — accepts `std::string`/`string_view` lvalues; **rvalue `std::string` is rejected at compile time** |
| `kv(string_view k, const char* v)` | string; `nullptr` ⇒ `null` |
| `kv(string_view k, const char (&v)[N])` | string literal / char array (length N-1; embedded `\0` kept) |
| `kv(string_view k, bool v)` | `true` / `false` |
| `kv(string_view k, std::nullptr_t)` | `null` |
| `template<integral T≠bool> kv(string_view k, T v)` | decimal integer; any int width incl. `char`/`wchar_t` (→ numeric code, `'A'` ⇒ `65`) |
| `nested_object(string_view k)` → `object_writer` | `"k":{` ; child closes with `}` on `end()`/destructor |
| `nested_array(string_view k)` → `array_writer` | `"k":[` |
| `end()` | `}` once |
| `as_known()` | Clang `-Wconsumed` helper; returns `*this` |

- **No floating-point support:** `kv("d", 1.5)` and pointer values (other than `char*`/`const char*`) are deleted overloads and fail to compile instead of silently writing `true`. Format floats yourself into a string, or avoid.
- Negative `int8_t`/`int16_t` values print correctly (`int8_t{-3}` → `-3`).

### `json::array_writer`
- **Ctor:** `explicit array_writer(sink out)` — writes `[`.
- **Members (return `array_writer&` unless noted):**

| Method | Writes |
|---|---|
| `val(string_view v)` | `"v"` |
| `val(const char* v)` | string / `null` for `nullptr` |
| `val(const char (&v)[N])` | string literal |
| `val(bool v)` | `true`/`false` |
| `val(std::nullptr_t)` | `null` |
| `template<integral T≠bool> val(T v)` | integer (same int8/int16 negative bug; `char` ⇒ number) |
| `obj()` → `object_writer` | starts an object element `{` |
| `end()` | `]` once |
| `as_known()` | Clang typestate helper |

No `nested_array`, no `val(double)` (rejected at compile time), no byte-string.

### `json::json_obj(fn)` → `json_obj_view<Fn>`
- `template<class Fn> constexpr auto json_obj(Fn&& fn)` (stores a decayed copy of the callable); `formatter<json_obj_view<Fn>>` builds an `object_writer(out)` around `fn(writer)`; **no spec accepted**. Lambda may take `auto&` or `json::object_writer&`.
- **Examples (all verified):**

```cpp
buffer_sink<512> b; { json::object_writer o(b.as_sink());
  o.kv("s","str").kv("t",true).kv("n",nullptr).kv("i",-5).kv("u",7u).kv("ll",INT64_MIN).kv("ull",UINT64_MAX).kv("c",'A').kv("u8",uint8_t{200});
  const char* np=nullptr; o.kv("cnull",np).kv("esc","a\"b\\c\n\t\x01\x1f/"); }
// -> {"s":"str","t":true,"n":null,"i":-5,"u":7,"ll":-9223372036854775808,"ull":18446744073709551615,"c":65,"u8":200,"cnull":null,"esc":"a\"b\\c\n\t\u0001\u001F/"}

buffer_sink<512> b; json::object_writer o(b.as_sink()); o.kv("name","dev");
{ auto n = o.nested_object("cfg"); n.kv("a",1).kv("b",2);
  auto na = n.nested_array("list"); na.val(1).val("two").val(true).val(nullptr);
  auto ob = na.obj(); ob.kv("x",1); ob.end(); na.end(); }
auto arr = o.nested_array("arr"); arr.val(1).val(-2).val("s").val(false).val(nullptr).val('x'); arr.end(); o.end();
// -> {"name":"dev","cfg":{"a":1,"b":2,"list":[1,"two",true,null,{"x":1}]},"arr":[1,-2,"s",false,null,120]}

json::array_writer a(b.as_sink()); a.val(1); a.end();   // -> [1]
{ json::object_writer e(b.as_sink()); } { json::array_writer a2(b.as_sink()); }  // -> {}[]

auto jv = json::json_obj([](auto& w){ w.kv("id",7).kv("ok",true); });
println("{} | {}", jv, jv);                         // -> {"id":7,"ok":true} | {"id":7,"ok":true}
format<128>("{{\"w\":{}}}", json::json_obj([](auto& w){ w.kv("a",1); }))  // -> {"w":{"a":1}}
json::json_obj([&](json::object_writer& w){ w.kv("x",x); auto a = w.nested_array("a"); a.val(x); }) // x=3 -> {"x":3,"a":[3]}
```
- **Notes:** a nested writer shares the parent sink: finish (`end()`/scope exit) a nested writer **before** writing to its parent again, or output interleaves incorrectly; nesting is not tracked at runtime. Floats unsupported (see above). No pretty-printing.

---

## `cbor.hpp` — streaming CBOR writer (`microfmt::cbor`, indefinite-length containers)

Output is **binary** (write to a `buffer_sink`/byte sink, not text). Containers use indefinite-length encoding: map starts `0xBF`, array `0x9F`, closed with break byte `0xFF` in the destructor or `end()` (idempotent). Writers take `sink` by value (like JSON), non-copyable/movable. Integers use the shortest encoding (1/2/3/5/9 bytes).

### `cbor::map_writer`
- **Include:** `<microfmt/formatters/cbor.hpp>`
- **Ctor:** `explicit map_writer(sink out)` (writes `0xBF`).
- **Members (return `map_writer&` unless noted):**

| Method | Encodes |
|---|---|
| `key(string_view k)` | text-string key (major 3) |
| `key(uint32_t int_key)` | unsigned-int key (major 0) — next call must write the value |
| `kv(string_view k, string_view v)` | key + text string |
| `kv(string_view k, const char* v)` | text string; `nullptr` ⇒ `0xF6` (null) |
| `kv(string_view k, const char (&v)[N])` | string literal |
| `kv(string_view k, span<const uint8_t> bytes)` | key + byte string (major 2); a `uint8_t[]` array converts implicitly |
| `kv(string_view k, bool v)` | `0xF5` true / `0xF4` false |
| `kv(string_view k, std::nullptr_t)` | `0xF6` |
| `template<integral T≠bool> kv(string_view k, T v)` | unsigned (major 0) or negative (major 1, `-1-v`) integer, any width |
| `template<integral T≠bool> kv(uint32_t k, T v)` | integer key + integer value (**only** integer values for integer keys) |
| `nested_map(string_view k)` → `map_writer` | key + `0xBF` |
| `nested_array(string_view k)` → `array_writer` | key + `0x9F` |
| `end()` | `0xFF` once |
| `as_known()` | Clang typestate helper |

- **No floating-point/half-float, no tags, no definite-length containers.** `kv("d", 1.5)` and pointer values are rejected at compile time.

### `cbor::array_writer`
- **Ctor:** `explicit array_writer(sink out)` (writes `0x9F`).
- **Members (return `array_writer&` unless noted):**

| Method | Encodes |
|---|---|
| `val(string_view v)` | text string |
| `val(const char* v)` | text / `null` (`nullptr`) |
| `val(const char (&v)[N])` | string literal |
| `val(span<const uint8_t> bytes)` | byte string |
| `val(bool v)` | `f5`/`f4` |
| `val(std::nullptr_t)` | `f6` |
| `template<integral T≠bool> val(T v)` | integer (shortest form; negative = major 1) |
| `map()` → `map_writer` | nested indefinite map element |
| `end()` | `0xFF` once |
| `as_known()` | Clang typestate helper |

No `nested_array` on array_writer (arrays can only hold maps/scalars here).

### `cbor::cbor_map(fn)` → `cbor_map_view<Fn>`
- `template<class Fn> constexpr auto cbor_map(Fn&& fn)`; `formatter<cbor_map_view<Fn>>` creates `map_writer(out)`, calls `fn(writer)`; **no spec accepted**. The binary result is written through the sink (so format into a binary-safe sink/`format<N>()` buffer and read it as bytes).
- **Examples (all verified; hex shown):**

```cpp
const uint8_t id[] = {0xDE,0xAD,0xBE,0xEF};
buffer_sink<512> b; { cbor::map_writer m(b.as_sink());
  m.kv("s","str").kv("sv",string_view("v")).kv("t",true).kv("f",false).kv("n",nullptr).kv("i",-5).kv("u",7u)
   .kv("big",300).kv("b",span<const uint8_t>(id)).kv("ll",int64_t{-1000}).kv("u64",uint64_t{1}<<40); const char* np=nullptr; m.kv("cn",np); }
// bf 61 73 63 73 74 72 | 62 73 76 61 76 | 61 74 f5 | 61 66 f4 | 61 6e f6 | 61 69 24 | 61 75 07 | 63 62 69 67 19 01 2c |
// 61 62 44 de ad be ef | 62 6c 6c 39 03 e7 | 63 75 36 34 1b 00 00 01 00 00 00 00 00 | 62 63 6e f6 | ff     (65 bytes)

{ cbor::map_writer m(b.as_sink()); m.kv(1u,10).kv(2u,-3).kv(300u,70000u); }
// bf 01 0a 02 22 19 01 2c 1a 00 01 11 70 ff   (integer keys; 300u → 19 01 2c, 70000 → 1a 00 01 11 70)

cbor::map_writer m(b.as_sink());
{ auto nm = m.nested_map("m"); nm.kv("a",1);
  auto na = m.nested_array("arr");            // NB: parent m used while nm still open (interleaved, see Notes)
  na.val(1).val(-1).val("t").val(span<const uint8_t>(id)).val(true).val(false).val(nullptr);
  const char* np = nullptr; na.val(np);
  auto om = na.map(); om.kv("k",0); om.end(); na.end(); nm.end(); }
m.key("raw").end(); m.end();
// bf 61 6d bf 61 61 01 63 61 72 72 9f 01 20 61 74 44 de ad be ef f5 f4 f6 f6 bf 61 6b 00 ff ff ff 63 72 61 77 ff
// (37 bytes; interleaving a parent write while a child is open and the dangling key("raw") before 0xFF both give invalid CBOR - do not copy that pattern)

auto cv = cbor::cbor_map([](auto& w){ w.kv("a",1); });
auto s = format<64>("{}", cv);   // bytes: bf 61 61 01 ff
```
- **Notes:** signed ints are encoded correctly for all widths (unlike JSON's small-int bug). Strings are not validated as UTF-8. Nested writers share the parent sink — end them in LIFO order.

---

## Quick index

| Want | Use | Spec |
|---|---|---|
| Print an address with fixed width (null aware) | `raw_ptr(p)`, `raw_ptr32(p)`, `raw_ptr(p, ptr_width_mode)` | `{:[p\|P\|x\|X\|32\|64\|0N\|z]}` |
| Print a contiguous pointer range | `raw_range(first,last)` / `raw_range(first,count)` | `{:[b\|c\|n]<elem-spec>}` |
| Decode register value with descriptor | `format_reg(val, reg_descriptor{name, bytes, reg_field{name,off,width,is_flag}...})` | `{:[s][n]}` |
| Decode `uint32_t` bit flags / fields | `bits(val, span<const bit_field>, show_raw_hex=true, sep=" \| ")` + `MICROFMT_BIT_FLAG/VALUE_DEC/VALUE_HEX` | none |
| Reusable register type with formatter | `MICROFMT_DEFINE_REGISTER_TYPE(Name, uint32_t, fields...)` | none |
| Hex dump of buffer | `hexdump(span, base, bpl=16, ascii=true)`, `hexdump_to(sink, span, base)` | none |
| Hex dump of faulting memory | `hexdump_checked(base, len, reader, ctx, bpl, ascii)`, `format_hexdump(view, linebuf, sink)` | none |
| MAC / EUI-64 | `mac(bytes, sep=':', upper=false)` | `{:[x\|X][:\|-\|.\|_]}` |
| Semantic version | `version(M,m,p,pre,build,show_v)`, `from_packed32/24(packed, show_v)` | `{:[#\|v\|c]}` |
| UUID | `uuid(bytes16, upper, braced)`; `boost::uuids::uuid` (opt-in) | `{:[X][#]}` |
| `std::variant` / `std::monostate` | plain value; `as_variant(v[,pre,suf])`, `as_debug_variant(v[,show_index])` | `{:[?\|i\|#]<elem-spec>}` / none |
| Register block as grid | `reg_grid_desc<W,N>{title,cols,{names}}` + `make_reg_grid(data, desc)` | none |
| CAN / CAN-FD frame | `can_frame(id11,payload,rtr)`, `can_extended(id29,payload,rtr)`, `can_fd(id,payload,ext,brs,esi)` | `{:[c\|C][x]}` |
| I2C transaction | `i2c_write/i2c_read(addr7,data,status)`, `i2c_10bit(addr10,dir,data,status)` | `{:[c\|C][x]}` |
| SPI transfer | `spi_duplex(mosi,miso,cs,mode,status)`, `spi_write(mosi,…)`, `spi_read(miso,…)` | `{:[c\|C][x]}` |
| Stream JSON | `json::object_writer`/`array_writer`, `json::json_obj(lambda)` | none |
| Stream CBOR | `cbor::map_writer`/`array_writer`, `cbor::cbor_map(lambda)` | none |

---

## Doc / header discrepancies found

- `register.hpp` Doxygen: `reg_field::is_flag` says "render as `+NAME`/`!NAME`" and `formatter` says "outer braces"/"`[...]`"; actual flag output is `NAME`/`!NAME` (no `+`), delimiters are `[ ]`; `n`/`N` output keeps a single leading space and no `]`.
- `register.hpp`: doc claims short mode shows "active flags only"; also skips zeroed multi-bit fields (same effect: any zero field is skipped).
- `grid_view.hpp`: documented CTAD deduction guide (`reg_grid_type_tag`) is not usable with aggregate init; spell `reg_grid_desc<W,N>` explicitly. Doxygen says "`name = 0xXXXX`" — actual is `NAME ` padded to 5 chars then `= 0x…`.
- `pointer.hpp`: Doxygen on `raw_ptr(const T*)` is mis-attached ("Create a null raw address view" / integer-address text sits on the wrong overload); `raw_ptr_view::address==0` produced via a typed pointer also sets `is_null`.
- `values-and-binary.md` variant section says `?`, `i`, `#` "include variant/index diagnostic information"; precise behavior: `?`⇒`variant(x)`, `i`/`#`⇒`variant[i](x)`, and `{:#}` on `as_debug_variant` still prints `variant(x)`.
- `semver.hpp` file comment says "parsing"; there is no parser, only formatting/construction.
- `docs/formatters/protocols-and-data.md`: states JSON/CBOR view "formatter has no custom specifier" (correct); does not mention that array_writer in JSON lacks nested arrays / CBOR array_writer lacks nested arrays, nor float non-support.
