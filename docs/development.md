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

## Build the complete compiler and architecture matrix

Run the matrix script to build Debug, Release, RelWithDebInfo, and MinSizeRel
with native GCC and Clang, plus ARM32, AArch64, and RISC-V 64 using both their
GNU cross compilers and Clang:

```bash
./scripts/build-matrix.sh
```

Native configurations run the complete test suite. Cross configurations build
examples, benchmarks, and all supported public-header language modes without
attempting to execute target binaries. The script requires CMake, Ninja,
ccache, and these cross compilers:

```text
arm-linux-gnueabihf-g++
aarch64-linux-gnu-g++
riscv64-linux-gnu-g++
```

The output defaults to `build-matrix/`. Environment variables can select a
smaller matrix or change execution behavior:

```bash
MICROFMT_BUILD_TARGETS="x86_64-clang arm32-gcc" \
MICROFMT_BUILD_TYPES="Debug MinSizeRel" \
./scripts/build-matrix.sh
```

Set `MICROFMT_MATRIX_CLEAN=1` to recreate selected build directories,
`MICROFMT_MATRIX_SKIP_TESTS=1` to compile without running native tests, or
`MICROFMT_MATRIX_BUILD_ROOT=/path/to/builds` to change the output directory.

Run Clang 24's unsafe-buffer analysis separately from the normal warning set:

```bash
./scripts/check-unsafe-buffer-usage.sh
```

This is a ratcheted migration check: it accepts the documented current
baseline but fails if a change introduces additional diagnostics. See
[Lifetime safety](lifetime-safety.md) for the boundary-annotation policy.

The CMake options `MICROFMT_BUILD_TESTS`, `MICROFMT_BUILD_EXAMPLES`,
`MICROFMT_BUILD_BENCHMARKS`, and `MICROFMT_BUILD_HEADER_CHECKS` can disable
unneeded targets for a smaller local build.

`MICROFMT_ENABLE_STRICT_WARNINGS` is enabled by default. It applies
compiler-specific GCC or Clang warning sets, including conversion,
sign-conversion, shadowing, alignment, and undefined-macro diagnostics.
Disable it only when integrating with a toolchain that cannot support the
project warning policy:

```bash
cmake -B build -DMICROFMT_ENABLE_STRICT_WARNINGS=OFF
```

Set `MICROFMT_ENABLE_WERROR` to promote these diagnostics to errors:

```bash
cmake -B build -DMICROFMT_ENABLE_WERROR=ON
```

Clang's unsafe-buffer analysis can be enabled independently. The option checks
that the selected compiler supports the warning and otherwise stops during
configuration. Diagnostics remain warnings by default, allowing an incremental
migration; enabling `MICROFMT_ENABLE_WERROR` promotes them to errors. The
ratchet script described above enforces that their number does not increase:

```bash
cmake -S . -B build-unsafe -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++-24 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DMICROFMT_ENABLE_UNSAFE_BUFFER_USAGE=ON
cmake --build build-unsafe
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

Use hardened microfmt containers for library-owned fixed storage and borrowed
data: `microfmt::array`, `microfmt::span`, `microfmt::string_view`, and
`microfmt::expected`. Introduce the corresponding standard type only for a
documented interoperability requirement or functionality that microfmt does
not provide. Convert standard views at the boundary rather than carrying
unchecked access through core implementation code.

Use `MICROFMT_STRING("...")` for literal, header-internal format strings.
This selects compile-time parsing and unrolled dispatch. Keep
`microfmt::string_view` paths for caller-provided runtime formats. Logging
macros are intentionally literal-only: they wrap their format argument
internally with `MICROFMT_STRING`.

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
