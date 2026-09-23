# microfmt/reloco codesize testbed

A self-contained testbed for measuring `microfmt`'s (and, through its
`reloco` formatter specializations, `reloco`'s) contribution to code size in
a realistic multi-file application, across toolchains, optimization levels,
and two deployment shapes:

1. **Single executable / single shared library** -- every generated
   `modules/module_NNN.cpp` linked into one `testbed_exe` and, separately,
   one `libtestbed.so`.
2. **`multiso`: one main app + N plugin `.so`'s** -- the generated modules
   partitioned into `N` shard `.so`'s (each independently `#include`-ing
   microfmt/reloco, as separate plugins/feature libraries typically do),
   linked into one `testbed_main`. This is the scenario that actually
   surfaces cross-`.so` code duplication, and is usually where the largest,
   most fixable optimization opportunities are.

It is **not** part of the main CMake build, the test suite, or CI -- build
and run it explicitly, as described below.

## Layout

- `common.hpp` -- the shared `point3` application type (with its own
  `microfmt::formatter` specialization) and the `TB_FMT(s)` macro that
  switches every generated call site between a runtime format string and
  `MICROFMT_STRING(s)`.
- `sink.cpp` -- the opaque, out-of-line output sink every module calls
  through, so the optimizer can never fold a call away or see through it
  the way it could an `inline`/header-only sink.
- `generate_modules.py` -- generates `modules/module_NNN.cpp` (one
  translation unit per "application source file") from a fixed catalog of
  ~23 *shapes*: distinct microfmt argument-type combinations covering both
  classic std types (`std::optional`, `std::vector`, `std::array`,
  `std::string`, `std::string_view`, `std::error_code`) and reloco's
  equivalents/extras (`reloco::optional`, `reloco::expected`,
  `reloco::vector`, `reloco::flat_map`, `reloco::non_zero`,
  `reloco::checked`/`saturating`/`wrapping`, `reloco::ordering`,
  `reloco::sso_string`), plus a user-defined struct and a hexdump. See the
  file's docstring and `SHAPE_CATALOG` for the exact list. Every call site
  gets its own message literal (so per-call-site `.rodata` scales with call
  sites, like a real app), while each *shape* -- and so the concrete
  `format_to<Args...>` instantiation -- repeats from the same fixed-size
  catalog, so growing `--count` adds call sites, not new instantiations.
  With `--lib-count N` it also partitions modules round-robin into
  `shards/lib_NN.cpp` + a `main_multiso.cpp`, for `multiso` mode.
- `main.cpp` / `lib.cpp` -- the single-executable / single-shared-library
  harnesses.
- `measure.py` -- wraps binutils `size`/`nm` into `sections`, `record` +
  `table` (the single-exe/so report), `topsymbols`, and `multiso` (the
  system-wide report + cross-`.so` duplicate-symbol analysis).
- `run.sh` -- generates, builds, and measures everything.

## Running it

```
tools/codesize/testbed/run.sh
```

Requires a C++20 compiler (default tries `g++` and `clang++`) and binutils
`size`/`nm` on `PATH`. microfmt depends on reloco's headers; if you don't
have a sibling `../reloco` checkout next to this repository, set
`MICROFMT_RELOCO_INCLUDE_DIR`:

```
MICROFMT_RELOCO_INCLUDE_DIR=/path/to/reloco/include tools/codesize/testbed/run.sh
```

Useful environment variables (see `run.sh`'s header comment for the full
list): `TESTBED_TOOLCHAINS`, `TESTBED_OPT_LEVELS`, `TESTBED_STRING_MODES`,
`TESTBED_MODULE_COUNT`, `TESTBED_NUM_SO`, `TESTBED_RUN_MULTISO=0` (to skip
the `multiso` phase). Add `--keep-build` to keep `build/` (all binaries,
plus `build/report.tsv`) around afterwards -- e.g. to run
`measure.py topsymbols`/`measure.py multiso` again by hand, or to compare
your own extra compiler flags against the recorded baseline.

## What we found running it (g++ 13, x86_64, `-O2`, 16 modules, 4 shards)

**Single exe/so: ~138 KB, dominated by microfmt/reloco's own code, not
call-site overhead.** `.text` is ~108 KB and `.rodata` ~14 KB for both the
executable and the single `.so` -- almost identical, as expected, since a
single artifact's linker already dedupes every `format_to<Args...>`
instantiation to one copy regardless of how many of the 368 call sites use
it.

**`MICROFMT_STRING` (compile-time/"static" mode) trades `.text` for less
`.rodata`, and the trade gets worse, not better, at `-O2` than `-Os`:** at
`-O2` it cost this build **+28 KB of `.text`** for -96 bytes of `.rodata`;
at `-Os` the cost dropped to +18 KB but growth is *linear per call site*
(each `MICROFMT_STRING(...)` call site is its own unrolled instantiation),
so it will keep growing with the number of static-string call sites in a
way the runtime-string mode does not. Reach for it only at call sites
you've profiled as hot, per `docs/usage.md`'s existing guidance -- this
testbed just gives you a way to put a number on "how much" for your own
code before deciding.

**`multiso` (4 shards + main): system total 234 KB, of which ~45.7 KB (about
20%) is pure cross-`.so` duplication.** Every shard `#include`s
microfmt/reloco independently, so each one gets its own copy of every
`format_to<Args...>`/`formatter<T>::format` instantiation the (identical,
in this synthetic app) argument-type catalog touches -- the linker can only
dedupe weak/COMDAT symbols *within* one `.so`'s link step, never across
separate `-shared` invocations. `measure.py multiso`'s duplicate-symbol
table ranks exactly which instantiations are responsible; in this run the
top offenders were `int_formatter_specs::format_int_impl<long/unsigned
long>`, `format_hexdump`, `reloco::basic_sso_string::try_append`, and
`vformat_to` itself -- i.e. the shared plumbing every formatted call goes
through, not any one shape.

**`-fvisibility=hidden -fvisibility-inlines-hidden` shrinks each shard `.so`
noticeably (78168 -> 65800 bytes measured here) but does *not* fix the
duplication `multiso` reports -- verify this yourself before trusting a
size regression test built on it.** We measured `.text`/`.rodata` unchanged
(in fact the hidden-visibility build had *slightly more* duplicated bytes:
46314 vs. 45693) and the shrinkage came entirely from smaller
`.dynsym`/`.dynstr`/relocation sections (`other`, not `text`/`rodata`,
13020 -> 4099 bytes here) -- expected, since hidden visibility only stops
those weak symbols from being *exported*, it doesn't change how many times
the compiler emitted their bodies. If you actually want to remove the
duplication (not just its dynamic-symbol-table overhead), you need a
structural fix instead:

- Factor the shared formatting glue (a small, stable set of
  `format_to<Args...>`/`formatter<T>` instantiations covering your app's
  actual argument-type catalog) into **one** base library every plugin
  `.so` links against instead of independently instantiating -- microfmt
  now ships exactly this mechanism: `MICROFMT_SHARED`/
  `MICROFMT_SHARED_BUILD` for microfmt's own built-in formatters, and
  `MICROFMT_FORMATTER_INSTANCE` (explicit template instantiation
  declarations/definitions) for your own `formatter<T>` instantiations
  over application-supplied types. See
  [`docs/shared-library.md`](../../../docs/shared-library.md) for the full
  usage guide.
- reloco (which microfmt's own reloco-container formatters, and this
  testbed's own shapes, depend on) ships the identical mechanism under its
  own names: `RELOCO_SHARED`/`RELOCO_SHARED_BUILD` for reloco's own mutex/
  allocator backends and char-based string aliases
  (`basic_string`/`basic_sso_string`/`basic_string_view<char>`), and
  `RELOCO_TYPE_INSTANCE` for your own class-template instantiations (e.g.
  `reloco::vector<MyType>`, `reloco::flat_map<K, V>`). `shared_common.cpp`
  and `shared_type_instances.hpp` in this directory wire *both* libraries'
  mechanisms into the one common `.so` at once -- see their own doc
  comments for the exact ordering/gotchas (in particular: types
  `reloco_compile.hpp` already bundles for the build side, like
  `basic_sso_string<char>`, still need their own consumer-side
  `RELOCO_TYPE_INSTANCE` in the app's own instance-list header, and
  whole-class instantiation can require more of a type -- e.g.
  `operator==` -- than the one member you actually call). See reloco's own
  `docs/shared-library.md` for its half of the guide.
- Or reduce the number of *distinct* argument-type combinations your
  plugins actually format (the thing `SHAPE_CATALOG` deliberately keeps
  fixed-size here) -- e.g. normalize logging call sites onto a smaller set
  of wrapper functions/types instead of ad hoc `format_to<Args...>` calls
  scattered across every plugin.
- Or, if the plugins are always loaded together anyway, merge them into
  fewer `.so`'s in the first place -- `multiso`'s "system total" makes the
  N=1 case (this testbed's plain `.so` mode) directly comparable to N>1.

## Adding a new shape

1. Write a `_your_shape(label, seed)` function in `generate_modules.py`
   returning one C++ statement string (wrap in `{ ... }` if it needs local
   declarations), using `TB_FMT("...")` for the format string so it
   respects `TESTBED_STATIC_STRINGS`.
2. Add any new `#include`s it needs to `common.hpp`.
3. Append it to `SHAPE_CATALOG`.
4. Re-run `tools/codesize/testbed/run.sh` and sanity-check the new shape's
   `format_to<Args...>` instantiation shows up (once) in
   `measure.py topsymbols <binary> --filter <your type>`.
