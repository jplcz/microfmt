<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Developing microfmt

This guide describes the local workflow and design constraints for contributors
to `microfmt`. The library is header-only; changes to a public header must
preserve the supported language-standard matrix and resource-constrained
runtime model.

## Configure a build

The default CMake configuration builds examples, benchmarks, tests, and
public-header checks:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
```

Use the smallest target that covers a change while iterating:

```bash
cmake --build build --target microfmt_tests
ctest --test-dir build --output-on-failure
cmake --build build --target check_public_headers
```

`check_public_headers` compiles the applicable public headers as C++17, C++20,
and, when available, C++23. It is required for changes under `include/`.

The CMake options `MICROFMT_BUILD_TESTS`, `MICROFMT_BUILD_EXAMPLES`,
`MICROFMT_BUILD_BENCHMARKS`, and `MICROFMT_BUILD_HEADER_CHECKS` can disable
unneeded targets for a smaller local build.

`MICROFMT_ENABLE_STRICT_WARNINGS` is enabled by default. It applies
compiler-specific GCC or Clang warning sets, including conversion,
sign-conversion, shadowing, alignment, and undefined-macro diagnostics, and
treats them as errors for microfmt targets. Disable it only when integrating
with a toolchain that cannot support the project warning policy:

```bash
cmake -B build -DMICROFMT_ENABLE_STRICT_WARNINGS=OFF
```

## Test changes

Tests use GoogleTest and are located in `tests/`. Add behavior-focused cases to
the existing feature-specific test source where one exists; otherwise add a
new `tests/test_<feature>.cpp` source and register it in the `microfmt_tests`
target in the root `CMakeLists.txt`.

Every test should exercise public behavior, including bounded-output and
failure paths where appropriate. Avoid tests that depend on heap allocation,
wall-clock timing, real hardware, or invalid memory accesses. Remote-inspector
tests should use `local_space_tag` and local fixtures rather than live process
memory.

Run a focused selection while iterating, for example:

```bash
ctest --test-dir build --output-on-failure -R 'CoreFormat|Inspector'
```

Then run the full test suite before submitting a change.

## Preserve library constraints

The core is intended for bare-metal, RTOS, ISR, and kernel-adjacent code.
Changes to core formatting and sink code must preserve these properties:

* No heap allocation, exceptions, virtual dispatch, or RTTI in core paths.
* `noexcept` APIs and bounded caller-visible storage.
* Writes streamed through `microfmt::sink` rather than temporary strings.
* C++17 compatibility unless an API is conditionally enabled for C++20 or
  C++23.

Use `MICROFMT_STRING("...")` for literal, header-internal format strings.
This selects compile-time parsing and unrolled dispatch. Keep
`std::string_view` paths for caller-provided runtime formats. Logging macros
are intentionally literal-only: they wrap their format argument internally
with `MICROFMT_STRING`.

When adding a new view or formatter, avoid hidden allocation and keep
temporary buffers explicit, caller-owned, and bounded. Prefer a lightweight
view object over copying or transforming a range before formatting it.

## Add a formatter

A formatter specialization implements `parse` and `format`. The parse function
should accept only the documented specifiers and remain `constexpr` and
`noexcept`; formatting must write directly to the supplied sink.

```cpp
template <> struct microfmt::formatter<widget_state> {
  constexpr void parse(microfmt::format_parse_context &ctx) noexcept {
    // Inspect ctx.spec() and retain only supported mode flags.
  }

  void format(const widget_state &value,
              const microfmt::sink &out) const noexcept {
    microfmt::format_to(out, MICROFMT_STRING("state={}"), value.code);
  }
};
```

Put optional functionality in a focused public header under
`include/microfmt/formatters/` and include the core header it needs. Add a
dedicated test file when the behavior does not naturally belong to an existing
test suite. If the new public header is not already pulled into
`tests/compile_all_headers.cpp`, add it so all supported standard modes compile
it.

## Documentation

Public headers use Doxygen comments. Add an `@file` block and document new
developer-facing classes, templates, functions, parameters, return values, and
non-obvious ownership or lifetime requirements.

The user and contributor guides are:

* [Usage guide](usage.md)
* [Developer guide](development.md)
* [Low-stack renderer guide](renderer-guide.md)
* [Inspector framework guide](inspector.md)

Generate the local API reference with:

```bash
doxygen Doxyfile
python3 -m http.server 8000 --directory build/docs/html
```

Open <http://localhost:8000> to inspect the rendered output. The `Doxyfile`
inputs include `include/`, `README.md`, and `docs/`, so these guides appear in
the generated reference.

## Review checklist

Before proposing a change:

1. Keep the patch scoped to the requested behavior.
2. Add or update tests for observable behavior.
3. Build `check_public_headers` after public-header changes.
4. Check the full relevant test suite.
5. Update Doxygen and user-facing documentation when public APIs or usage
   change.
