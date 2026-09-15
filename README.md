<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: MIT
-->

# microfmt

<img src="docs/microfmt-logo.svg" alt="microfmt logo" width="128">

A zero-allocation, deterministic, and low-overhead C++ formatting library engineered specifically for resource-constrained environments (bare-metal embedded systems, real-time operating systems, ISRs, and kernel-space drivers).

`microfmt` provides Python/`std::format`-style sequential formatting syntax with `noexcept` APIs, `-fno-exceptions`/`-fno-rtti` compatibility, and a strictly bounded stack footprint.

---

## Key Features

* **Zero-allocation, `noexcept` formatting:** Formats directly to a sink without heap allocation, exceptions, virtual dispatch, or RTTI. Integer conversion uses a fixed 24-byte scratch buffer.
* **C++17+ header-only core:** Supports sequential `{}` replacement fields, escaped braces (`{{` and `}}`), decimal and hexadecimal (`x`/`X`) integers, and zero-padded widths such as `{:04x}`. C++20 and C++23 features are enabled only when their standard-library APIs are available.
* **Extensible formatters:** Define `microfmt::formatter<T>` specializations for application types. Built-in formatters cover strings, character arrays, integral values, booleans, pointers, and `nullptr`.
* **Flexible output sinks:** Stream to a type-erased callback, bounded external buffer (`span_sink`), inline fixed buffer (`buffer_sink`), null-terminated buffer (`c_string_sink`), output iterator, callback, byte counter, or discard sink. `stdio.hpp` adds `FILE*`, stdout/stderr, and POSIX file-descriptor sinks.
* **Circular trace buffering:** Retain the most recent formatted output in a fixed-size, power-of-two `ring_buffer_sink`, overwriting old data on overflow and dumping retained content chronologically for post-mortem diagnostics.
* **Span support:** Includes a small C++17-compatible `microfmt::span` and interoperates with `std::span` when it is available.
* **Range formatting:** Join iterator pairs or ranges with runtime delimiters and element format specifications, or use compile-time `join_as` delimiters and element specs.
* **Binary and diagnostic views:** Format integers as binary with prefixes, explicit widths, and nibble grouping; render named bitfields and synthesized register types; produce direct or fault-checked hex dumps with ASCII panes.
* **Embedded-friendly value adapters:** Format fixed-point values, escaped strings and byte buffers, UUIDs, hexadecimal/binary wrapper values, human-readable byte counts, address offsets, memory ranges, aligned text, and joined spans.
* **Terminal and document output:** Emit ANSI colors and attributes with a runtime color toggle, compose aligned, quoted, case-transformed, and truncated text views, and generate Markdown headings, lists, code blocks, block quotes, and aligned tables.
* **Optional ecosystem bridges:** Use `fmt.hpp` for a lightweight `{fmt}`-style compatibility surface (`format`, `format_to`, `format_to_n`, `print`, `println`, and custom `fmt::formatter`s). `boost_describe.hpp` formats reflected Boost.Describe enums and public members; `uuid.hpp` optionally accepts `boost::uuids::uuid`.
* **Compile-time format strings & unrolled dispatch:** Parse format strings during constant evaluation via `MICROFMT_STRING(...)` to validate syntax and unroll formatting at compile time. This eliminates dynamic parsing loops, indirect function pointer thunks, and stack-allocated argument arrays (`arg_ptrs`), reducing stack frame footprints by up to 90% for constrained embedded targets.

---

## Format Specifier Reference

Format arguments are consumed in order. A replacement field has the form
`{}` or `{:specifier}`.

| Syntax | Applies to | Meaning | Example output |
|---|---|---|---|
| `{}` | All formatters | Default representation | `format<32>("id={}", 42)` → `id=42` |
| `{{` / `}}` | Literal text | Escaped opening/closing brace | `format<32>("{{{}}}", 42)` → `{42}` |
| `{:N}` | Integers and pointers | Minimum width, zero-padded; a sign is outside the padded digits | `{:4}` with `42` → `0042` |
| `{:0N}` | Integers and pointers | Explicit zero-padded minimum width | `{:04}` with `42` → `0042` |
| `{:x}` | Integers | Lowercase hexadecimal | `{:04x}` with `26` → `001a` |
| `{:X}` | Integers | Uppercase hexadecimal | `{:04X}` with `26` → `001A` |
| `{:N}` | Pointers | Minimum-width, zero-padded hexadecimal address digits after `0x` | `{:08}` with `0x1000` → `0x00001000` |

Some formatting views define additional specifiers:

| Header | Specifier | Meaning |
|---|---|---|
| `formatters/binary.hpp` | `#` | Add the `0b` binary prefix |
| `formatters/binary.hpp` | `_` | Group binary digits into nibbles with underscores |
| `formatters/can.hpp` | `c` | Use compact SocketCAN `candump` output |
| `formatters/i2c.hpp` | `c` | Use compact I2C trace output |
| `formatters/spi.hpp` | `c` | Use compact SPI trace output |
| `formatters/can.hpp`, `formatters/i2c.hpp`, `formatters/spi.hpp` | `x` | Use lowercase hexadecimal digits |

---

## Public API

Include the headers for the facilities you use. Every API below is in
`microfmt` unless another namespace is shown.

| Header | Developer-facing APIs |
|---|---|
| `microfmt/microfmt.hpp` | `span<T>`, `sink`, `span_sink`, `buffer_sink<N>`, `c_string_sink<N>`, `iterator_sink<It>`, `counting_sink`, `null_sink`, `callback_sink<F>`, `make_callback_sink`, `format_to`, `vformat_to`, `format<N>`, and the `formatter<T>` customization point |
| `microfmt/sinks/container_sink.hpp` | `container_sink<Container>`, `make_container_sink`, `format_to_container(container, ...)`, and `format_as_container<Container>(...)` for growable character containers |
| `microfmt/sinks/pmr_sink.hpp` | PMR-backed `string`, `vector`, `format`, `format_vector`, and `arena_sink` facilities |
| `microfmt/sinks/ring_buffer_sink.hpp` | `ring_buffer_sink<Capacity>` for a circular output buffer; `Capacity` must be a non-zero power of two. Use `as_sink`, `view`, `dump_to`, `size`, `capacity`, `empty`, `full`, and `reset` |
| `microfmt/log/logger.hpp` | `basic_logger`, structured log records, and nullable `default_logger()` / `set_default_logger()`; define `MICROFMT_ENABLE_DEFAULT_LOGGER` to opt into the stdout-backed fallback |
| `microfmt/log/macros.hpp` | `MICROFMT_LOGGER_*` macros for explicit loggers, plus optional `MICROFMT_LOG_*` macros; define `MICROFMT_DEFAULT_LOGGER` to an application logger before including this header |
| `microfmt/sinks/stdio.hpp` | `file_sink`, `stdout_sink`, `stderr_sink`, POSIX `fd_sink`, plus `print` and `println` overloads for stdout, `FILE*`, and POSIX file descriptors |
| `microfmt/sinks/styled_sink.hpp` | `transform_sink` with `char_transform`, `prefix_sink`, and `limit_sink` for zero-buffer output adaptation |
| `microfmt/sinks/syslog_sink.hpp` | `log::syslog_sink<Capacity>` adapter for structured `log::log_msg` records |
| `microfmt/sinks/android_log_sink.hpp` | `log::android_log_sink<MessageCapacity, TagCapacity>` adapter for structured Android logcat records |
| `microfmt/sinks/systemd_sink.hpp` | `log::systemd_sink<MessageCapacity, IdentifierCapacity>` adapter for structured systemd journal records |
| `microfmt/sinks/tizen_dlog_sink.hpp` | `log::tizen_dlog_sink<TagCapacity>` adapter for structured Tizen DLOG records |
| `microfmt/formatters/ranges.hpp` | `join(range, delimiter)`, `join(first, last, delimiter)`, and compile-time `join_as<Delimiter, ElementSpec>(...)` |
| `microfmt/formatters/format_helpers.hpp` | `hex`, `bin`, `bytes`, `addr_offset`, `mem_range`, `align`, and `join(span, delimiter)` |
| `microfmt/formatters/binary.hpp` | `binary_view`, fixed-width `bin`, `bin<Bits>`, `bin_prefixed`, and `bin_grouped` |
| `microfmt/formatters/can.hpp` | `can_frame`, `can_extended`, `can_fd`, and diagnostic or candump-style CAN frame formatting |
| `microfmt/formatters/cbor.hpp` | `map_writer`, `array_writer`, byte/text values, and `cbor_map` format-string adapters |
| `microfmt/formatters/i2c.hpp` | `i2c_write`, `i2c_read`, `i2c_10bit`, and diagnostic or compact I2C transaction formatting |
| `microfmt/formatters/json.hpp` | `object_writer`, `array_writer`, JSON escaping, and `json_obj` format-string adapters |
| `microfmt/formatters/spi.hpp` | `spi_duplex`, `spi_write`, `spi_read`, and diagnostic or compact SPI transfer formatting |
| `microfmt/formatters/base_views.hpp` | `base64` for byte spans and arrays, plus custom-size `bin_grouped(value, group_size, separator)` |
| `microfmt/formatters/escaped.hpp` | `escaped` overloads for string views, character buffers, byte buffers, and spans |
| `microfmt/formatters/filter_view.hpp` | `filter(range, predicate)` and `filter(pointer, count, predicate)` for zero-allocation filtered range formatting; select `b`, `c`, or `n` delimiters and forward element specifiers |
| `microfmt/formatters/fixed_point.hpp` | `fixed<Scale, Decimals>`, `milli`, `centi`, `micro`, and the `milli_view`, `centi_view`, and `micro_view` aliases |
| `microfmt/formatters/semver.hpp` | `semver`, `version`, `from_packed32`, and `from_packed24` |
| `microfmt/formatters/units.hpp` | `scale_base`, `with_unit`, `auto_si`, `auto_bytes`, and `hertz` |
| `microfmt/formatters/chrono.hpp` | Formatters for `std::chrono::duration`, system-clock timestamps, and steady-clock uptime values |
| `microfmt/formatters/math.hpp` | `vec`, owning `vec3`, and row-major `mat<T, Rows, Cols>` views |
| `microfmt/formatters/map_view.hpp` | `map_view` for standard, iterator-based, or custom-extractor key/value ranges; select `b`, `c`, or `n` delimiters and forward element specifiers |
| `microfmt/formatters/monad.hpp` | `std::optional` formatting and, in C++23, `std::expected` formatting |
| `microfmt/formatters/pointer.hpp` | `raw_ptr`, `raw_ptr32`, `ptr_width_mode`, and `raw_range` for deterministic native or compatibility-width addresses and contiguous pointer ranges |
| `microfmt/formatters/source_location.hpp` | `source_loc`, `source_loc_view`, and direct source-location formatters when a supported source-location API is enabled |
| `microfmt/formatters/tuple.hpp` | C++17 tuple-like formatting for `std::tuple`, `std::pair`, and compatible types; select `b`, `c`, `n`, or `p` delimiters and forward element specifiers |
| `microfmt/formatters/bitfield.hpp` | `bit_type`, `bit_field`, `bitfield_view`, `bits`, `MICROFMT_BIT_FLAG`, `MICROFMT_BIT_VALUE_DEC`, `MICROFMT_BIT_VALUE_HEX`, and `MICROFMT_DEFINE_REGISTER_TYPE` |
| `microfmt/formatters/hexdump.hpp` | `memory_reader_fn_t`, `hexdump`, `hexdump_checked`, and `hexdump_to` |
| `microfmt/formatters/uuid.hpp` | `uuid` overloads for 16-byte data and, when enabled, `boost::uuids::uuid` |
| `microfmt/formatters/net.hpp` | `mac` for MAC-48/EUI-48 and EUI-64 byte sequences |
| `microfmt/formatters/ansi.hpp` | `microfmt::ansi::color`, `attribute`, `style`, predefined styles, `styled`, and color helpers such as `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, and `gray` |
| `microfmt/formatters/styled.hpp` | `styled_str_view`, `pad`, `pad_center`, `pad_right`, `to_upper`, `to_lower`, `truncate`, and `quoted` for zero-allocation text presentation |
| `microfmt/markdown.hpp` | `microfmt::md::align`, `column`, and fluent `writer` methods for text, headings, lists, block quotes, code blocks, and tables |
| `microfmt/formatters/fmt.hpp` | `fmt::format`, `fmt::format_to`, `fmt::format_to_n`, `fmt::print`, `fmt::println`, `fmt::join`, and the `fmt::formatter<T>` bridge |
| `microfmt/formatters/boost_describe.hpp` | Automatic `formatter<T>` support for Boost.Describe reflected enums, structs, and classes |
| `microfmt/sinks/tee_sink.hpp` | `tee_sink<N>` and `make_tee` for broadcasting output to a fixed number of sinks |
| `microfmt/inspector/address_space.hpp` | `address_space_ref`, `address_space_traits<Tag>`, `remote_string_view`, and `remote_ref<T>` for fault-aware target reads |
| `microfmt/inspector/address_translator.hpp` | `address_translator_ref`, `address_translator_traits<Tag>`, and `translation_attributes` for virtual-to-physical translation |
| `microfmt/inspector/memory_classifier.hpp` | `memory_classifier_ref`, `memory_classifier_traits<Tag>`, `memory_region_info`, and `memory_region_type` |
| `microfmt/inspector/memory_scanner.hpp` | `memory_scanner<AbiTraits>` and `address_source_ref` for classified register/address scanning with bounded ASCII hex dumps |
| `microfmt/inspector/dwarf_registers.hpp` | Architecture register indexes, generic-timer IDs, and constexpr GPR/system/address-candidate catalogs |
| `microfmt/inspector/register_context.hpp` | `register_context_ref` and `register_context_vtable` for type-erased target register access |
| `microfmt/inspector/register_view.hpp` | `register_context_view<AbiTraits>` for architecture-aware register rendering |

---

## Compile time strings

Compile-time string evaluation in microfmt shifts format string parsing and argument dispatch from runtime loops to 
constant evaluation. This eliminates dynamic parsing overhead, runtime indirection thunks, and stack-allocated 
argument pointer arrays.

### The MICROFMT_STRING Macro

Because standard C++17 lacks class-type non-type template parameters (NTTP), microfmt uses a static provider lambda pattern.
The `MICROFMT_STRING` macro wraps a string literal into a unique static type holding a `constexpr std::string_view` 
getter without runtime overhead.

```cpp
#include "microfmt/microfmt.hpp"

// Creates a unique static type provider for the format string
auto fmt_str = MICROFMT_STRING("Value: {}, Status: {}");
```

### Available Compile-Time APIs

Every core formatting and printing function provides an overload accepting `compile_string_holder` alongside runtime `std::string_view` fallbacks.

* Formatting to Buffers & Sinks

```cpp
// Fixed-size stack buffer sink
auto buf = microfmt::format<64>(MICROFMT_STRING("Sensor ID: {}, Temp: {}C"), id, temp);

// Writing to an explicit sink
microfmt::format_to(my_sink, MICROFMT_STRING("Data: 0x{:08X}"), val);
```

* Direct Printing Helpers:

```cpp
// stdout printing
microfmt::print(MICROFMT_STRING("Connected to server on port {}\n"), port);
microfmt::println(MICROFMT_STRING("System ready. Active tasks: {}"), count);

// Target FILE* stream
microfmt::println(stderr, MICROFMT_STRING("Error code: {}"), err_code);

// POSIX file descriptor (e.g., UART or socket)
microfmt::println(uart_fd, MICROFMT_STRING("AT+SEND={}\r\n"), len);
```

### Under the Hood: Unrolled Format Dispatch

When using `MICROFMT_STRING`, the library evaluates the format string during constant folding:

* Breaks the format string into static literal slices and argument indices at compile time.
* Dispatches formatting via std::index_sequence without building a runtime `const void* arg_ptrs[]` stack array.
* Inlines trivial formatters (such as integers, booleans, and characters) directly into sequential write operations.

---

## Language Standards and Source Locations

All non-Boost public headers compile in C++17, C++20, and C++23. The
`join_as` compile-time delimiter adapter is available in C++20 and later, and
the `std::expected` formatter is available only in C++23 when the standard
library supplies it.

`microfmt/formatters/source_location.hpp` uses `std::source_location` in
C++20 and later. In a C++17 build, including that header is harmless but does
not expose source-location views unless Boost support is explicitly requested.
Define `MICROFMT_ENABLE_BOOST_SOURCE_LOCATION` before including it to enable
`boost::source_location` support; this requires
`<boost/assert/source_location.hpp>`. With that opt-in, pass
`BOOST_CURRENT_LOCATION` to `microfmt::source_loc(...)`, or format
`BOOST_CURRENT_LOCATION` directly. Boost is never required for the core
library.

The default CMake build keeps its C++20 project setting. Its
`microfmt_headers_cxx17`, `microfmt_headers_cxx20`, and, when supported,
`microfmt_headers_cxx23` object targets compile every applicable public header.
Build the `check_public_headers` target to run those compile-only checks.

---

## Cheat Sheet

```cpp
#include <microfmt/microfmt.hpp>

// Format to a fixed-size inline buffer.
auto message = microfmt::format<64>("id={}, value=0x{:04X}", 42, 0x1af);
std::string_view text = message.view();

// Format to an existing buffer; excess output is safely truncated.
char storage[32];
microfmt::span_sink buffer{microfmt::span<char>{storage}};
microfmt::format_to(buffer.as_sink(), "status={}", true);

// Stream formatted output to a callback, device driver, or logger.
auto write = [](std::string_view chunk) noexcept { /* transmit chunk */ };
auto callback = microfmt::make_callback_sink(write);
microfmt::format_to(callback.as_sink(), "temperature={:03}", 24);

// Format through an output iterator.
char output[16];
char* end = microfmt::format_to(output, "{}", 123);

// Count required output size without storing the output.
microfmt::counting_sink counter;
microfmt::format_to(counter.as_sink(), "name={}, id={}", "dev0", 7);
std::size_t required = counter.count();
```

```cpp
#include <microfmt/sinks/ring_buffer_sink.hpp>
#include <microfmt/sinks/stdio.hpp>

// Keep the latest 256 bytes of formatted trace output. Capacity is a power of two.
microfmt::ring_buffer_sink<256> trace;
microfmt::format_to(trace.as_sink(), "[{}] sensor={}\n", 123, 42);

// Emit retained bytes from oldest to newest, even after the buffer wraps.
trace.dump_to(microfmt::stdout_sink());
```

```cpp
#include <microfmt/formatters/ranges.hpp>
#include <microfmt/formatters/format_helpers.hpp>
#include <microfmt/formatters/binary.hpp>

uint8_t bytes[] = {0x12, 0x34, 0x56};
auto joined = microfmt::join(bytes, ":");
auto hex_value = microfmt::hex(0x2a, 4, true);
auto binary = microfmt::bin_prefixed(uint8_t{0x2a}, true);

auto message = microfmt::format<96>(
    "bytes=[{}], value={}, bits={}", joined, hex_value, binary);
// bytes=[18:52:86], value=0x002a, bits=0b0010_1010
```

```cpp
#include <microfmt/formatters/escaped.hpp>
#include <microfmt/formatters/fixed_point.hpp>
#include <microfmt/formatters/uuid.hpp>

uint8_t id[16] = {};
auto message = microfmt::format<128>(
    "name={}, voltage={} V, id={:#X}",
    microfmt::escaped("line\nbreak"),
    microfmt::milli(3300),
    microfmt::uuid(id));
// name="line\nbreak", voltage=3.300 V,
// id={00000000-0000-0000-0000-000000000000}
```

```cpp
#include <microfmt/formatters/ansi.hpp>
#include <microfmt/sinks/stdio.hpp>

microfmt::println("{}", microfmt::ansi::red("error"));
microfmt::ansi::style::colors_enabled = false; // Disable escape sequences.
microfmt::println(stderr, "failed with code {}", 5);
```

```cpp
#include <microfmt/formatters/styled.hpp>

auto row = microfmt::format<64>(
    "|{:*^12u}| {:>16tq.9} |",
    microfmt::pad_right("ready", 5),
    microfmt::to_lower("FIRMWARE_UPDATE_PENDING"));
// |***READY****|      "Firmw..." |
```

```cpp
#include <microfmt/sinks/styled_sink.hpp>

microfmt::buffer_sink<64> storage;
microfmt::prefix_sink output{storage.as_sink(), "[telemetry] "};
microfmt::format_to(output.as_sink(), "id={}\nvoltage={} mV", 3, 3295);
// [telemetry] id=3
// [telemetry] voltage=3295 mV
```

```cpp
#include <microfmt/formatters/tuple.hpp>
#include <tuple>

auto registers = std::make_tuple(0x00A1, 0x000F, 0xBEEF);
auto row = microfmt::format<64>("registers={:b04X}", registers);
// registers=[00A1, 000F, BEEF]
```

```cpp
#include <microfmt/formatters/map_view.hpp>
#include <map>

std::map<int, const char*> states{{1, "boot"}, {3, "ready"}};
auto line = microfmt::format<64>("states={}", microfmt::map_view(states));
// states={1: boot, 3: ready}
```

```cpp
#include <microfmt/formatters/pointer.hpp>

const uint16_t registers[] = {0x00A1, 0x000F};
auto address = microfmt::format<32>("pc={:32P}",
                                    microfmt::raw_ptr(uintptr_t{0x08001234}));
auto values = microfmt::format<32>("regs={:c04X}",
                                   microfmt::raw_range(registers, size_t{2}));
// pc=0X08001234
// regs={00A1, 000F}
```

```cpp
#include <microfmt/formatters/filter_view.hpp>

const uint16_t registers[] = {0x0001, 0x000A, 0x001F};
auto active = microfmt::format<32>(
    "active={:c04X}", microfmt::filter(registers, [](uint16_t value) {
      return value >= 0x0010;
    }));
// active={001F}
```

To support a custom type, specialize `microfmt::formatter<T>` with `parse` and
`format` methods; see the custom formatter example below.

---

## Static Stack Usage vs. libc `snprintf`

Under `MinSizeRel` (`-Oz` / `-Os`), `microfmt` enforces flat caller overhead and bounded leaf frames. Unlike `snprintf`, which demands large caller destination buffers and deep execution frames, `microfmt` streams directly into the target sink.

| Test Scenario | Arguments | `microfmt` Caller Frame | `libc snprintf` Caller Frame | `microfmt` Dynamic Total Peak |
| :--- | :---: | :---: | :---: | :---: |
| **Static Text (0 args)** | 0 | **72 B** | 0 B | **~192 B** |
| **Integers (2 args)** | 2 | **88 B** | 72 B | **~296 B** |
| **Mixed Log (4 args)** | 4 | **120 B** | 120 B | **~328 B** |
| **Driver Payload (6 args)** | 6 | **136 B** | 136 B | **~344 B** |
| **Full Register Context (8 args)** | 8 | **184 B** | 248 B *(WARN)* | **~392 B** |
| **System State Snapshot (10 args)** | 10 | **184 B** | 328 B *(FAIL)* | **~392 B** |

> *Measurements gathered via `llvm-readelf --stack-sizes` on x86_64 target with Clang.*

---

## Quick Start

### Formatting to an Inline Stack Buffer (`buffer_sink`)

```cpp
#include <microfmt/microfmt.hpp>

void log_example() {
    // Reserves a 64-byte stack buffer and formats into it safely
    auto result = microfmt::format<64>("Device [{}] status: 0x{:04x}, temp: {} C", "sensor0", 0x1A, -4);

    // Read non-owning std::string_view
    std::string_view view = result.view();
}
```

### Formatting to a Fixed External Buffer (`span_sink`)

```cpp
#include <microfmt/microfmt.hpp>

void format_into_raw_array(char* out_buf, size_t out_len) {
    microfmt::span_sink sink(microfmt::span<char>(out_buf, out_len));
    
    microfmt::format_to(sink.as_sink(), "Packet ID: #{:02x}, Len: {}", 42, 512);
}
```

### Direct Streaming to UART / Hardware Callbacks

```cpp
#include <microfmt/microfmt.hpp>

void uart_put_sv(void* ctx, std::string_view sv) noexcept {
    for (char c : sv) {
        UART0_DR = c; // Write directly to hardware register
    }
}

void panic(uint32_t err_code, void* pc) {
    microfmt::sink out{nullptr, uart_put_sv};
    microfmt::format_to(out, "KERNEL PANIC: Code=0x{:08x}, PC={}\n", err_code, pc);
}
```

### Custom Type Formatter

```cpp
enum class LogLevel : uint8_t { DEBUG, INFO, WARN, ERROR };

template <>
struct microfmt::formatter<LogLevel> {
    constexpr void parse(format_parse_context&) noexcept {}

    void format(LogLevel level, const sink& out) const noexcept {
        switch (level) {
            case LogLevel::DEBUG: out.write("DBG"); break;
            case LogLevel::INFO:  out.write("INF"); break;
            case LogLevel::WARN:  out.write("WRN"); break;
            case LogLevel::ERROR: out.write("ERR"); break;
        }
    }
};
```

---

## Integration

`microfmt` is a header-only library with no external dependencies beyond the C++ standard library headers.

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    microfmt
    GIT_REPOSITORY https://github.com/jplcz/microfmt.git
)
FetchContent_MakeAvailable(microfmt)

target_link_libraries(my_embedded_app PRIVATE microfmt::microfmt)
```

## API Documentation

The guides in [`docs/usage.md`](docs/usage.md),
[`docs/development.md`](docs/development.md), and
[`docs/renderer-guide.md`](docs/renderer-guide.md), and
[`docs/inspector.md`](docs/inspector.md) cover library integration,
contributor workflows, low-stack renderer design, and remote-memory
inspection. The API reference documents every public header.

Generate the Doxygen API reference locally with:

```bash
doxygen Doxyfile
```

The HTML output is written to `build/docs/html/index.html`.
Preview it locally with:

```bash
python3 -m http.server 8000 --directory build/docs/html
```

Then open [localhost:8000](http://localhost:8000).

The **Publish API documentation** workflow publishes the generated HTML to
GitHub Pages on pushes to `master` that change public headers or documentation.
The published reference is available at
[jplcz.github.io/microfmt](https://jplcz.github.io/microfmt/).
To enable deployment, configure **Settings > Pages > Build and deployment** to
use **GitHub Actions** as the source. The workflow can also be run manually
from the Actions tab.

## Running Tests and Stack Analysis

### Building & Running Unit Tests

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --target microfmt_tests
./build/tests/microfmt_tests
```

### Checking Public Headers Across Language Standards

```bash
cmake --build build --target check_public_headers
```

### Enforcing Stack Budgets

To verify that all probed functions stay within the default 256-byte stack limit:

```bash
cmake --build build --target check_stack_usage
```

Example report output:

#### GCC 15.2.0

```text
Scanned files: 1
  Stack Size |   Budget |   Status | Function Name
--------------------------------------------------------------------------------
       368 B |    256 B |    REF | void probe_libc_snprintf_10_mixed_system_state(uint8_t, int16_t, uint32_t, const char*, const char*, bool, uint64_t, void*, char, uint32_t)
       288 B |    256 B |    REF | void probe_libc_snprintf_8_context(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)
       224 B |    256 B |   WARN | void probe_stack_10_mixed_system_state(uint8_t, int16_t, uint32_t, const char*, std::string_view, bool, uint64_t, void*, char, uint32_t)
       208 B |    256 B |    REF | void probe_libc_snprintf_6_mixed_log(uint32_t, const char*, char, int32_t, void*, bool)
       192 B |    256 B |     PASS | void probe_microfmt_8_context(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)
       160 B |    256 B |     PASS | void probe_stack_6_mixed_log(uint32_t, const char*, char, int32_t, void*, bool)
       160 B |    256 B |     PASS | void probe_stack_compiled_10_mixed_system_state(uint8_t, int16_t, uint32_t, const char*, std::string_view, bool, uint64_t, void*, char, uint32_t)
       160 B |    256 B |    REF | void probe_libc_snprintf_4_mixed(uint32_t, const char*, uintptr_t, void*)
       144 B |    256 B |     PASS | void microfmt::formatter<T, typename std::enable_if<((is_integral_v<T> && (! is_same_v<T, bool>)) && (! is_same_v<T, char>)), void>::type>::format(T, const microfmt::sink&) const [with T = short int]
       144 B |    256 B |     PASS | void microfmt::formatter<T, typename std::enable_if<((is_integral_v<T> && (! is_same_v<T, bool>)) && (! is_same_v<T, char>)), void>::type>::format(T, const microfmt::sink&) const [with T = int]
       144 B |    256 B |     PASS | void microfmt::vformat_to(const sink&, std::string_view, span<const void* const>, span<void (* const)(const void*, std::basic_string_view<char>, const sink&) noexcept>)
       144 B |    256 B |     PASS | void probe_microfmt_4_mixed(uint32_t, const char*, uintptr_t, void*)
       144 B |    256 B |     PASS | void probe_stack_compiled_6_mixed_log(uint32_t, const char*, char, int32_t, void*, bool)
       128 B |    256 B |     PASS | void microfmt::formatter<T, typename std::enable_if<((is_integral_v<T> && (! is_same_v<T, bool>)) && (! is_same_v<T, char>)), void>::type>::format(T, const microfmt::sink&) const [with T = long unsigned int]
       128 B |    256 B |     PASS | void probe_microfmt_compiled_8_context(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t)
       112 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = void*]
       112 B |    256 B |     PASS | void microfmt::formatter<T, typename std::enable_if<((is_integral_v<T> && (! is_same_v<T, bool>)) && (! is_same_v<T, char>)), void>::type>::format(T, const microfmt::sink&) const [with T = unsigned char]
       112 B |    256 B |     PASS | void microfmt::formatter<T, typename std::enable_if<((is_integral_v<T> && (! is_same_v<T, bool>)) && (! is_same_v<T, char>)), void>::type>::format(T, const microfmt::sink&) const [with T = unsigned int]
       112 B |    256 B |     PASS | void probe_microfmt_2_integers(uint32_t, uint64_t)
       112 B |    256 B |     PASS | void probe_microfmt_compiled_4_mixed(uint32_t, const char*, uintptr_t, void*)
       112 B |    256 B |    REF | void probe_libc_snprintf_2_integers(uint32_t, uint64_t)
        96 B |    256 B |     PASS | void probe_microfmt_compiled_2_integers(uint32_t, uint64_t)
        80 B |    256 B |     PASS | void probe_microfmt_0_args()
        48 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = const char*]
        32 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = char]
        16 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = unsigned char]
        16 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = long unsigned int]
        16 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = unsigned int]
        16 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = short int]
        16 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = int]
         8 B |    256 B |     PASS | static constexpr void {anonymous}::volatile_sink::as_sink()::<lambda(void*, std::string_view)>::_FUN(void*, std::string_view)
         8 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = std::basic_string_view<char>]
         8 B |    256 B |     PASS | void microfmt::detail::format_type_thunk(const void*, std::string_view, const microfmt::sink&) [with T = bool]
         8 B |    256 B |     PASS | void probe_microfmt_compiled_0_args()
         8 B |    256 B |    REF | void probe_libc_snprintf_0_args()
--------------------------------------------------------------------------------
[SUCCESS] All microfmt functions are within the 256B stack budget.
```

Benchmark:

```text
===================================================================
   microfmt Runtime Stack High-Water Mark Benchmark (1-Byte Res)   
===================================================================
Baseline Call Overhead: 28 Bytes
-------------------------------------------------------------------
Benchmark Probe | Peak Stack | Net Overhead
-------------------------------------------------------------------
microfmt runtime vformat_to (10 args) | 488 B | 460 B
microfmt compiled MICROFMT_STRING (10 args) | 264 B | 236 B
===================================================================
```

#### Clang 24

```text
=== Static Stack Usage Report (CLANG) ===
Scanned files: 1
  Stack Size |   Budget |   Status | Function Name
--------------------------------------------------------------------------------
       328 B |    256 B |    REF | probe_libc_snprintf_10_mixed_system_state
       248 B |    256 B |    REF | probe_libc_snprintf_8_context
       168 B |    256 B |     PASS | probe_microfmt_8_context
       168 B |    256 B |     PASS | probe_stack_10_mixed_system_state
       136 B |    256 B |    REF | probe_libc_snprintf_6_mixed_log
       120 B |    256 B |     PASS | probe_stack_6_mixed_log
       120 B |    256 B |    REF | probe_libc_snprintf_4_mixed
       120 B |    256 B |     PASS | microfmt::vformat_to
       104 B |    256 B |     PASS | probe_microfmt_4_mixed
       104 B |    256 B |     PASS | probe_stack_compiled_6_mixed_log
       104 B |    256 B |     PASS | probe_stack_compiled_10_mixed_system_state
        88 B |    256 B |     PASS | probe_microfmt_compiled_4_mixed
        88 B |    256 B |     PASS | microfmt::formatter<unsigned long, void>::format
        88 B |    256 B |     PASS | microfmt::detail::format_type_thunk<void*>
        88 B |    256 B |     PASS | microfmt::formatter<int, void>::format
        88 B |    256 B |     PASS | microfmt::formatter<short, void>::format
        72 B |    256 B |     PASS | probe_microfmt_0_args
        72 B |    256 B |     PASS | probe_microfmt_2_integers
        72 B |    256 B |     PASS | probe_microfmt_compiled_8_context
        72 B |    256 B |    REF | probe_libc_snprintf_2_integers
        56 B |    256 B |     PASS | microfmt::formatter<unsigned int, void>::format
        56 B |    256 B |     PASS | microfmt::formatter<unsigned char, void>::format
        40 B |    256 B |     PASS | probe_microfmt_compiled_2_integers
        24 B |    256 B |     PASS | microfmt::detail::format_type_thunk<char const*>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned int>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned long>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<char>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<int>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned char>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<short>
         0 B |    256 B |     PASS | probe_microfmt_compiled_0_args
         0 B |    256 B |    REF | probe_libc_snprintf_0_args
         0 B |    256 B |     PASS | (anonymous namespace)::volatile_sink::as_sink()::'lambda'(void*, std::basic_string_view<char, std::char_traits<char>>)::__invoke
         0 B |    256 B |     PASS | microfmt::detail::format_type_thunk<bool>
         0 B |    256 B |     PASS | microfmt::detail::format_type_thunk<std::basic_string_view<char, std::char_traits<char>>>
--------------------------------------------------------------------------------
[SUCCESS] All microfmt functions are within the 256B stack budget.
```

Benchmark:

```text
===================================================================
   microfmt Runtime Stack High-Water Mark Benchmark (1-Byte Res)   
===================================================================
Baseline Call Overhead: 20 Bytes
-------------------------------------------------------------------
Benchmark Probe | Peak Stack | Net Overhead
-------------------------------------------------------------------
microfmt runtime vformat_to (10 args) | 3640 B | 3620 B
microfmt compiled MICROFMT_STRING (10 args) | 256 B | 236 B
===================================================================
```

## License

Distributed under the MIT License. See `LICENSE` for details.
