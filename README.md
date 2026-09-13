# microfmt

A zero-allocation, deterministic, and low-overhead C++ formatting library engineered specifically for resource-constrained environments (bare-metal embedded systems, real-time operating systems, ISRs, and kernel-space drivers).

`microfmt` provides Python/`std::format`-style sequential formatting syntax with `noexcept` APIs, `-fno-exceptions`/`-fno-rtti` compatibility, and a strictly bounded stack footprint.

---

## Key Features

* **Zero-allocation, `noexcept` formatting:** Formats directly to a sink without heap allocation, exceptions, virtual dispatch, or RTTI. Integer conversion uses a fixed 24-byte scratch buffer.
* **C++20 header-only core:** Supports sequential `{}` replacement fields, escaped braces (`{{` and `}}`), decimal and hexadecimal (`x`/`X`) integers, and zero-padded widths such as `{:04x}`.
* **Extensible formatters:** Define `microfmt::formatter<T>` specializations for application types. Built-in formatters cover strings, character arrays, integral values, booleans, pointers, and `nullptr`.
* **Flexible output sinks:** Stream to a type-erased callback, bounded external buffer (`span_sink`), inline fixed buffer (`buffer_sink`), null-terminated buffer (`c_string_sink`), output iterator, callback, byte counter, or discard sink. `stdio.hpp` adds `FILE*`, stdout/stderr, and POSIX file-descriptor sinks.
* **Circular trace buffering:** Retain the most recent formatted output in a fixed-size, power-of-two `ring_buffer_sink`, overwriting old data on overflow and dumping retained content chronologically for post-mortem diagnostics.
* **Span support:** Includes a small C++17-compatible `microfmt::span` and interoperates with `std::span` when it is available.
* **Range formatting:** Join iterator pairs or ranges with runtime delimiters and element format specifications, or use compile-time `join_as` delimiters and element specs.
* **Binary and diagnostic views:** Format integers as binary with prefixes, explicit widths, and nibble grouping; render named bitfields and synthesized register types; produce direct or fault-checked hex dumps with ASCII panes.
* **Embedded-friendly value adapters:** Format fixed-point values, escaped strings and byte buffers, UUIDs, hexadecimal/binary wrapper values, human-readable byte counts, address offsets, memory ranges, aligned text, and joined spans.
* **Terminal and document output:** Emit ANSI colors and attributes with a runtime color toggle, and generate Markdown headings, lists, code blocks, block quotes, and aligned tables.
* **Optional ecosystem bridges:** Use `fmt.hpp` for a lightweight `{fmt}`-style compatibility surface (`format`, `format_to`, `format_to_n`, `print`, `println`, and custom `fmt::formatter`s). `boost_describe.hpp` formats reflected Boost.Describe enums and public members; `uuid.hpp` optionally accepts `boost::uuids::uuid`.

---

## Public API

Include the headers for the facilities you use. Every API below is in
`microfmt` unless another namespace is shown.

| Header | Developer-facing APIs |
|---|---|
| `microfmt/microfmt.hpp` | `span<T>`, `sink`, `span_sink`, `buffer_sink<N>`, `c_string_sink<N>`, `iterator_sink<It>`, `counting_sink`, `null_sink`, `callback_sink<F>`, `make_callback_sink`, `format_to`, `vformat_to`, `format<N>`, and the `formatter<T>` customization point |
| `microfmt/sinks/ring_buffer_sink.hpp` | `ring_buffer_sink<Capacity>` for a circular output buffer; `Capacity` must be a non-zero power of two. Use `as_sink`, `view`, `dump_to`, `size`, `capacity`, `empty`, `full`, and `reset` |
| `microfmt/sinks/stdio.hpp` | `file_sink`, `stdout_sink`, `stderr_sink`, POSIX `fd_sink`, plus `print` and `println` overloads for stdout, `FILE*`, and POSIX file descriptors |
| `microfmt/sinks/syslog_sink.hpp` | `log_priority`, line-buffered `syslog_sink<Capacity>`, and the scoped `syslog` helper |
| `microfmt/formatters/ranges.hpp` | `join(range, delimiter)`, `join(first, last, delimiter)`, and compile-time `join_as<Delimiter, ElementSpec>(...)` |
| `microfmt/formatters/format_helpers.hpp` | `hex`, `bin`, `bytes`, `addr_offset`, `mem_range`, `align`, and `join(span, delimiter)` |
| `microfmt/formatters/binary.hpp` | `binary_view`, fixed-width `bin`, `bin<Bits>`, `bin_prefixed`, and `bin_grouped` |
| `microfmt/formatters/escaped.hpp` | `escaped` overloads for string views, character buffers, byte buffers, and spans |
| `microfmt/formatters/fixed_point.hpp` | `fixed<Scale, Decimals>`, `milli`, `centi`, `micro`, and the `milli_view`, `centi_view`, and `micro_view` aliases |
| `microfmt/formatters/bitfield.hpp` | `bit_type`, `bit_field`, `bitfield_view`, `bits`, `MICROFMT_BIT_FLAG`, `MICROFMT_BIT_VALUE_DEC`, `MICROFMT_BIT_VALUE_HEX`, and `MICROFMT_DEFINE_REGISTER_TYPE` |
| `microfmt/formatters/hexdump.hpp` | `memory_reader_fn_t`, `hexdump`, `hexdump_checked`, and `hexdump_to` |
| `microfmt/formatters/uuid.hpp` | `uuid` overloads for 16-byte data and, when enabled, `boost::uuids::uuid` |
| `microfmt/formatters/ansi.hpp` | `microfmt::ansi::color`, `attribute`, `style`, predefined styles, `styled`, and color helpers such as `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, and `gray` |
| `microfmt/markdown.hpp` | `microfmt::md::align`, `column`, and fluent `writer` methods for text, headings, lists, block quotes, code blocks, and tables |
| `microfmt/formatters/fmt.hpp` | `fmt::format`, `fmt::format_to`, `fmt::format_to_n`, `fmt::print`, `fmt::println`, `fmt::join`, and the `fmt::formatter<T>` bridge |
| `microfmt/formatters/boost_describe.hpp` | Automatic `formatter<T>` support for Boost.Describe reflected enums, structs, and classes |

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

## Running Tests and Stack Analysis

### Building & Running Unit Tests

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build --target microfmt_tests
./build/tests/microfmt_tests
```

### Enforcing Stack Budgets

To verify that all probed functions stay within the default 256-byte stack limit:

```bash
cmake --build build --target check_stack_usage
```

Example report output:

```text
=== Static Stack Usage Report (CLANG) ===
  Stack Size |   Budget |   Status | Function Name
--------------------------------------------------------------------------------
       328 B |    256 B |    REF | probe_libc_snprintf_10_mixed_system_state
       248 B |    256 B |    REF | probe_libc_snprintf_8_context
       184 B |    256 B |     PASS | probe_microfmt_8_context
       184 B |    256 B |     PASS | probe_stack_10_mixed_system_state
       136 B |    256 B |     PASS | probe_stack_6_mixed_log
       136 B |    256 B |    REF | probe_libc_snprintf_6_mixed_log
       120 B |    256 B |     PASS | probe_microfmt_4_mixed
       120 B |    256 B |    REF | probe_libc_snprintf_4_mixed
       120 B |    256 B |     PASS | microfmt::vformat_to
        88 B |    256 B |     PASS | probe_microfmt_2_integers
        88 B |    256 B |     PASS | microfmt::formatter<unsigned int, void>::format
        88 B |    256 B |     PASS | microfmt::formatter<unsigned long, void>::format
        88 B |    256 B |     PASS | microfmt::detail::format_type_thunk<void*>
        88 B |    256 B |     PASS | microfmt::formatter<int, void>::format
        88 B |    256 B |     PASS | microfmt::formatter<unsigned char, void>::format
        88 B |    256 B |     PASS | microfmt::formatter<short, void>::format
        72 B |    256 B |     PASS | probe_microfmt_0_args
        72 B |    256 B |    REF | probe_libc_snprintf_2_integers
        24 B |    256 B |     PASS | microfmt::detail::format_type_thunk<char const*>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned int>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned long>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<char>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<int>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<unsigned char>
         8 B |    256 B |     PASS | microfmt::detail::format_type_thunk<short>
         0 B |    256 B |    REF | probe_libc_snprintf_0_args
         0 B |    256 B |     PASS | (anonymous namespace)::volatile_sink::as_sink()::'lambda'(void*, std::basic_string_view<char, std::char_traits<char>>)::__invoke
         0 B |    256 B |     PASS | microfmt::detail::format_type_thunk<bool>
         0 B |    256 B |     PASS | microfmt::detail::format_type_thunk<std::basic_string_view<char, std::char_traits<char>>>
--------------------------------------------------------------------------------
[SUCCESS] All microfmt functions are within the 256B stack budget.
```

## License

Distributed under the MIT License. See `LICENSE` for details.
