<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Developing jplcz_microfmt

This guide describes the local workflow and design constraints for contributors
to `jplcz_microfmt`. The library is header-only; changes to a public header must
preserve the supported language-standard matrix and resource-constrained
runtime model.

## Editor and formatting integration

The repository provides shared configuration for common editors and language
tools:

* `.editorconfig` defines UTF-8, LF line endings, final newlines, and
  language-specific indentation.
* `.clangd` enables background indexing, include diagnostics, focused
  bug-prone/performance/portability checks, and include-aware completion.
* `.clang-format` defines the C++ formatting style.
* `.cmake-format.yaml` defines the CMake formatting style.

Configure CMake with a compilation database so clangd receives the exact flags
for each C++17, C++20, or C++23 target:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_COMPILER=clang++-24
```

clangd searches the `build/` directory automatically. Format CMake files with:

```bash
cmake-format -i CMakeLists.txt examples/CMakeLists.txt
```

## Configure a build

The default CMake configuration builds examples, benchmarks, tests, and
public-header checks:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build
```

Use the smallest target that covers a change while iterating:

```bash
cmake --build build --target jplcz_microfmt_tests
ctest --test-dir build --output-on-failure
cmake --build build --target jplcz_microfmt_check_public_headers
```

`jplcz_microfmt_check_public_headers` compiles the applicable public headers as C++17, C++20,
and, when available, C++23. It is required for changes under `include/`.

### The `jplcz_reloco` dependency

`jplcz_microfmt` depends on [`jplcz_reloco`](https://github.com/jplcz/reloco)
for hardened container and lifetime-safety primitives. By default it is
fetched with `FetchContent` from `JPLCZ_MICROFMT_RELOCO_GIT_REPOSITORY` at
`JPLCZ_MICROFMT_RELOCO_GIT_TAG` (a pinned commit or tag). Point
`JPLCZ_MICROFMT_RELOCO_SOURCE_DIR` at a local checkout to override this, for
example when developing both projects together or vendoring a pinned copy:

```bash
cmake -B build -G Ninja \
  -DJPLCZ_MICROFMT_RELOCO_SOURCE_DIR=/path/to/reloco
```

`jplcz_reloco`'s own tests, header checks, and strict warnings are disabled
for this embedded build regardless of its own defaults.
`JPLCZ_MICROFMT_INSTALL` also controls `jplcz_reloco`'s install rules, so an
installed `jplcz_microfmt` package always installs and exports the matching
`jplcz_reloco` package alongside it, and `find_package(jplcz_microfmt)`
transitively resolves `jplcz_reloco` through `find_dependency`.

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
JPLCZ_MICROFMT_BUILD_TARGETS="x86_64-clang arm32-gcc" \
JPLCZ_MICROFMT_BUILD_TYPES="Debug MinSizeRel" \
./scripts/build-matrix.sh
```

Set `JPLCZ_MICROFMT_MATRIX_CLEAN=1` to recreate selected build directories,
`JPLCZ_MICROFMT_MATRIX_SKIP_TESTS=1` to compile without running native tests, or
`JPLCZ_MICROFMT_MATRIX_BUILD_ROOT=/path/to/builds` to change the output directory.

Run Clang 24's unsafe-buffer analysis separately from the normal warning set:

```bash
./scripts/check-unsafe-buffer-usage.sh
```

This is a ratcheted migration check: it accepts the documented current
baseline but fails if a change introduces additional diagnostics. See
[Lifetime safety](lifetime-safety.md) for the boundary-annotation policy.

The CMake options `JPLCZ_MICROFMT_BUILD_TESTS`, `JPLCZ_MICROFMT_BUILD_EXAMPLES`,
`JPLCZ_MICROFMT_BUILD_BENCHMARKS`, and `JPLCZ_MICROFMT_BUILD_HEADER_CHECKS` can disable
unneeded targets for a smaller local build.

Set `JPLCZ_MICROFMT_BUILD_MANPAGES=ON` to additionally generate `man(7)` pages
from the Markdown docs via `pandoc` (fails the configure step if `pandoc` is
not on `PATH`); see
[Installable CMake package](usage.md#installable-cmake-package) for the
generated page names and install location. This is unrelated to the
Doxygen HTML API reference produced by `scripts/build-docs.sh`.

`JPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS` is enabled by default. It applies
compiler-specific GCC or Clang warning sets, including conversion,
sign-conversion, shadowing, alignment, and undefined-macro diagnostics.
Disable it only when integrating with a toolchain that cannot support the
project warning policy:

```bash
cmake -B build -DJPLCZ_MICROFMT_ENABLE_STRICT_WARNINGS=OFF
```

Set `JPLCZ_MICROFMT_ENABLE_WERROR` to promote these diagnostics to errors:

```bash
cmake -B build -DJPLCZ_MICROFMT_ENABLE_WERROR=ON
```

Clang's unsafe-buffer analysis can be enabled independently. The option checks
that the selected compiler supports the warning and otherwise stops during
configuration. Diagnostics remain warnings by default, allowing an incremental
migration; enabling `JPLCZ_MICROFMT_ENABLE_WERROR` promotes them to errors. The
ratchet script described above enforces that their number does not increase:

```bash
cmake -S . -B build-unsafe -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++-24 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DJPLCZ_MICROFMT_ENABLE_UNSAFE_BUFFER_USAGE=ON
cmake --build build-unsafe
```

## Continuous integration

GitHub Actions run the following independent validation lanes:

* `CI` builds and tests with GCC, Clang, AppleClang, and MSVC, promotes project
  warnings to errors, and compiles every public header in each supported
  language mode.
* `Cross-compile` builds native x86-64 and ARM32, AArch64, and RISC-V 64
  configurations.
* `Sanitizers` runs the complete test executable with AddressSanitizer and
  UndefinedBehaviorSanitizer.
* `Package managers` creates and consumes the Conan 2 package, installs and
  consumes the vcpkg overlay port, verifies CPM.cmake integration, and builds
  CPack archive/DEB/RPM packages.
* `CodeQL` performs scheduled and change-triggered C++ security analysis.
* `Dependency review` rejects vulnerable dependency changes in pull requests
  when the repository has GitHub dependency review available.
* `Publish API documentation` builds and deploys the Doxygen site after
  documentation or public-header changes on `master`.

All workflows use read-only repository permissions unless a GitHub service
requires a narrowly scoped permission. Concurrent runs for the same ref are
cancelled when a newer commit supersedes them. Dependabot checks the referenced
GitHub Actions weekly.

Build, sanitizer, cross-compilation, package-manager, and CodeQL jobs are
skipped while a pull request is a draft. Marking it ready for review triggers
those jobs; converting it back to a draft triggers the workflows again so
their concurrency groups cancel obsolete in-progress work. Lightweight
dependency review remains enabled for draft pull requests.

Native CI, cross-compilation, sanitizers, and CodeQL first classify changed
paths. Their expensive jobs run only when relevant CMake configuration, public
headers, tests, examples, benchmarks, supporting scripts, or workflow
definitions change. Package-manager workflows use equivalent trigger-level
path filters. Documentation, licenses, editor configuration, and other
metadata-only changes retain lightweight workflow results without consuming
build runners. Manual dispatch always runs the requested jobs, and scheduled
CodeQL analysis always runs.

The native equivalents are the configure, build, `ctest`, header-check, matrix,
unsafe-buffer, and package-manager commands documented in this guide and in
[Package-manager integration](package-managers.md).

### Run build workflows locally

The build-oriented workflows call repository scripts directly. Run the same
scripts from the project root:

```bash
# Native configure, build, tests, and public-header checks
CXX=clang++-24 ./scripts/run-native-ci.sh build-ci Release \
  -DCMAKE_CXX_COMPILER=clang++-24

# AddressSanitizer and UndefinedBehaviorSanitizer
CXX=clang++-24 ./scripts/run-sanitizers.sh build-sanitizers

# Complete native and cross-compiler matrix
./scripts/build-matrix.sh

# Clang unsafe-buffer diagnostic ratchet
./scripts/check-unsafe-buffer-usage.sh

# Conan 2 package and its test_package consumer
./scripts/check-conan-package.sh

# vcpkg overlay package and find_package consumer
VCPKG_ROOT=/path/to/vcpkg ./scripts/check-vcpkg-package.sh

# CPM.cmake local-checkout consumer
./scripts/check-cpm-package.sh

# CPack archive, DEB, and RPM packages
./scripts/check-cpack-package.sh

# Doxygen output under build/docs/html
./scripts/build-docs.sh
```

The scripts accept environment variables for repeatable local customization:

| Variable | Purpose |
|---|---|
| `JPLCZ_MICROFMT_BUILD_PARALLEL` | Parallel build job count; defaults to `2` |
| `JPLCZ_MICROFMT_CONAN_HOME` | Isolated Conan cache and profile directory |
| `JPLCZ_MICROFMT_CONAN_COMPILER_VERSION` | Override Conan's compiler-version model when the installed compiler is newer than Conan's settings |
| `JPLCZ_MICROFMT_VCPKG_BUILD_DIR` | vcpkg consumer build directory |
| `JPLCZ_MICROFMT_VCPKG_TRIPLET` | vcpkg triplet; defaults to `x64-linux` |
| `JPLCZ_MICROFMT_CPM_BUILD_DIR` | CPM.cmake consumer build directory |
| `JPLCZ_MICROFMT_CPM_PATH` | Existing CPM.cmake file instead of downloading one |
| `JPLCZ_MICROFMT_CPM_VERSION` | CPM.cmake release downloaded when no local file is supplied |
| `JPLCZ_MICROFMT_CPACK_BUILD_DIR` | CPack build directory |

Additional arguments after the documented positional parameters are forwarded
to CMake or Conan. The package-manager scripts require their corresponding
tools to be installed; the vcpkg script additionally requires a bootstrapped
checkout referenced by `VCPKG_ROOT`.

CodeQL analysis, dependency review, and Pages deployment still require GitHub
services. Their underlying C++ build and Doxygen generation are covered by the
local scripts above.

## Test changes

Tests use GoogleTest and are located in `tests/`. Add behavior-focused cases to
the existing feature-specific test source where one exists; otherwise add a
new `tests/test_<feature>.cpp` source and register it in the `jplcz_microfmt_tests`
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

Use `MICROFMT_STRING("...")` for literal, header-internal format strings on
hot paths where the unrolled code size is a net win. Because a formatter's
`format` method may be instantiated for many call sites, prefer
`microfmt::string_view` when the same literal would otherwise be unrolled
repeatedly for little benefit. Keep `microfmt::string_view` paths for
caller-provided runtime formats. Logging macros are intentionally
literal-only: they wrap their format argument internally with
`MICROFMT_STRING`.

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
3. Build `jplcz_microfmt_check_public_headers` after public-header changes.
4. Check the full relevant test suite.
5. Update Doxygen and user-facing documentation when public APIs or usage
   change.
