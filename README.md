# microfmt

A zero-allocation, deterministic, and low-overhead C++ formatting library engineered specifically for resource-constrained environments (bare-metal embedded systems, real-time operating systems, ISRs, and kernel-space drivers).

`microfmt` provides Python/`std::format`-style formatting syntax with compile-time format string validation, strict `noexcept` guarantees, full `-fno-exceptions`/`-fno-rtti` compatibility, and a strictly bounded stack footprint.

---

## Key Features

* **Zero Dynamic Allocations:** Never touches the heap (`malloc`/`free` or `new`/`delete`).
* **Strictly `noexcept` & Freestanding:** Fully functional with `-fno-exceptions` and `-fno-rtti`.
* **Deterministic, Bounded Stack Usage:** Integral conversion routines share a compact 24-byte scratchpad within leaf frames to prevent deep stack growth.
* **Type-Erased Sinks:** Lightweight sink abstraction for streaming output directly into custom ring buffers, UART interfaces, fixed arrays, or iterators.
* **Modern C++ Support:** First-class C++20 support with clean C++17 runtime fallback.

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
