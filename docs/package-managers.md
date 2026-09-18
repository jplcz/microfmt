<!--
SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>

SPDX-License-Identifier: BSD-2-Clause
-->

# Package-manager integration

`jplcz_microfmt` provides native metadata for Conan 2, a vcpkg overlay port,
and direct CPM.cmake integration. Every integration exposes the same CMake
target:

```cmake
find_package(jplcz_microfmt CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

The package is header-only and requires C++17 or later. Its core has no
third-party dependencies. Headers for optional Boost formatters include Boost
directly, so consumers that use those headers must add the corresponding Boost
packages themselves.

## Conan 2

The repository root contains a Conan 2 recipe. Create it in the local Conan
cache from a checkout:

```sh
conan profile detect --force
conan create . --build=missing
```

The resulting reference is `jplcz_microfmt/0.1.0`. A consuming
`conanfile.txt` can request it and generate CMake integration:

```ini
[requires]
jplcz_microfmt/0.1.0

[generators]
CMakeDeps
CMakeToolchain

[layout]
cmake_layout
```

Install dependencies and configure the consumer with Conan's generated
toolchain:

```sh
conan install . --build=missing -s compiler.cppstd=17
cmake --preset conan-release
cmake --build --preset conan-release
```

For a single-config generator without Conan-generated presets, pass
`-DCMAKE_TOOLCHAIN_FILE=<output-folder>/conan_toolchain.cmake` and an explicit
`CMAKE_BUILD_TYPE` when configuring CMake.

The recipe uses Conan's `header-library` package type and clears the package ID,
so operating-system, architecture, compiler, and build-type differences do not
produce duplicate binary packages. `CMakeDeps` generates
`jplcz_microfmtConfig.cmake` with the canonical
`jplcz_microfmt::microfmt` target.

## vcpkg

The repository contains a checkout-local overlay port at
`packaging/vcpkg/ports/jplcz-microfmt`. Install it with:

```sh
vcpkg install jplcz-microfmt \
  --overlay-ports=/absolute/path/to/microfmt/packaging/vcpkg/ports
```

Configure a consumer through the vcpkg toolchain:

```sh
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

The consumer then uses the standard `find_package` and target shown at the top
of this guide. The vcpkg package name uses a hyphen because vcpkg port names do
not use underscores; this does not change the CMake package or target names.

The included overlay deliberately packages the current checkout. This makes
local development and pinned source checkouts reproducible without embedding a
moving Git reference in the port. A registry submission should replace the
local `SOURCE_PATH` with `vcpkg_from_github`, an immutable release tag, and its
verified SHA-512 archive hash.

## CPM.cmake

Pin a release tag or commit in the consuming project:

```cmake
include(cmake/CPM.cmake)

CPMAddPackage(
    NAME jplcz_microfmt
    GITHUB_REPOSITORY jplcz/microfmt
    GIT_TAG <release-tag-or-commit>
    OPTIONS
        "JPLCZ_MICROFMT_BUILD_TESTS OFF"
        "JPLCZ_MICROFMT_BUILD_EXAMPLES OFF"
        "JPLCZ_MICROFMT_BUILD_BENCHMARKS OFF"
        "JPLCZ_MICROFMT_BUILD_HEADER_CHECKS OFF"
        "JPLCZ_MICROFMT_INSTALL OFF"
)

target_link_libraries(my_target PRIVATE jplcz_microfmt::microfmt)
```

Use an immutable tag or full commit hash rather than a moving branch. The
development and installation options already default to off when the project
is embedded, but listing them explicitly keeps dependency behavior visible and
stable if the top-level project changes its cache.

For a local checkout, replace `GITHUB_REPOSITORY` and `GIT_TAG` with:

```cmake
SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/jplcz_microfmt"
```

No CPM-specific target alias is needed; `CPMAddPackage` delegates to the
project's normal `add_subdirectory` integration.

## Other dependency managers

Other dependency managers that consume CMake projects directly can use the
same `add_subdirectory` interface documented in
[Using jplcz_microfmt](usage.md#fetchcontent-and-add_subdirectory). Managers
that consume installed CMake config packages can use the normal installation
described in [Using jplcz_microfmt](usage.md#installable-cmake-package).

## CPack

Standalone, top-level configurations with `JPLCZ_MICROFMT_INSTALL=ON` (the
default) also enable CPack. Build and package with:

```sh
cmake -S . -B build \
  -DJPLCZ_MICROFMT_BUILD_TESTS=OFF \
  -DJPLCZ_MICROFMT_BUILD_EXAMPLES=OFF \
  -DJPLCZ_MICROFMT_BUILD_BENCHMARKS=OFF \
  -DJPLCZ_MICROFMT_BUILD_HEADER_CHECKS=OFF
cmake --build build
cpack --config build/CPackConfig.cmake
```

Archive generators (`TGZ`, `ZIP`) are always available. `DEB` and `RPM`
generators are added automatically when `dpkg-deb` or `rpmbuild` are found on
the host; since the package is header-only, both are built as
architecture-independent (`all`/`noarch`) packages. Run
`cpack --config build/CPackSourceConfig.cmake` for a source archive, which
excludes VCS metadata and build directories.

Installing any generated package places headers under `include/` and CMake
package files under `lib/cmake/jplcz_microfmt/`, matching the layout produced
by `cmake --install`; see
[Installable CMake package](usage.md#installable-cmake-package) for how
consumers locate the installed package. `scripts/check-cpack-package.sh`
builds every available generator and verifies the archive package's contents.
