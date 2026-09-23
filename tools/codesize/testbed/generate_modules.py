#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
"""Generates the codesize testbed's "application" translation units.

Each generated modules/module_NNN.cpp stands in for one source file of a
typical multi-file application built against microfmt (and, through
microfmt's reloco formatter specializations, reloco): it calls a handful of
logging/formatting call sites drawn from a fixed catalog of *shapes*
(distinct argument-type combinations covering both classic std types --
std::optional, std::vector, std::array, std::string, std::string_view,
std::error_code -- and reloco's equivalents/extras -- reloco::optional,
reloco::expected, reloco::vector, reloco::flat_map, reloco::non_zero,
reloco::checked/saturating/wrapping, reloco::ordering -- plus a
user-defined struct with its own microfmt::formatter specialization and a
hexdump). Every call site gets its own, module-unique message literal (so
per-call-site .rodata scales with the number of call sites, as it would in
a real app), while the shape -- and therefore the concrete
format_to<Args...> template instantiation -- repeats across modules from
the same fixed-size catalog, so growing --count adds call sites, not new
instantiations.

Usage:
    generate_modules.py --out-dir DIR [--count N] [--shapes-per-module K]
"""
import argparse
import os
import textwrap

# Each shape is a distinct argument-type combination formatted through
# microfmt. `emit(label, seed)` returns the one C++ statement (a string) for
# that shape, using `label` (a C string literal, unique per call site) and
# `seed` (an int, varied per call site) to keep the literal payload -- but
# not the *type* signature -- distinct across call sites.


def _static(label, seed):
    del seed
    return f'microfmt::format_to(out, TB_FMT("{label} system nominal, no faults detected\\n"));'


def _int(label, seed):
    return f'microfmt::format_to(out, TB_FMT("{label} retry count={{}}\\n"), {seed * 7 + 3});'


def _int2(label, seed):
    return f'microfmt::format_to(out, TB_FMT("{label} range=[{{}}, {{}}]\\n"), {seed}, {seed + 42});'


def _hex_u32(label, seed):
    v = (seed * 2654435761) & 0xFFFFFFFF
    return f'microfmt::format_to(out, TB_FMT("{label} status=0x{{:08x}}\\n"), std::uint32_t{{{v}u}});'


def _point(label, seed):
    p = f"testbed::point3{{{seed}, {seed + 1}, {seed + 2}}}"
    return f'microfmt::format_to(out, TB_FMT("{label} position={{}}\\n"), {p});'


def _hexdump(label, seed):
    n = 8 + (seed % 24)
    bytes_init = ", ".join(str((seed + i * 17) & 0xFF) for i in range(n))
    return (
        f"{{ static constexpr std::uint8_t payload[] = {{{bytes_init}}}; "
        f'microfmt::format_to(out, TB_FMT("{label} payload:\\n{{}}"), '
        f"microfmt::hexdump(microfmt::span<const std::uint8_t>(payload, sizeof(payload)), {seed}u)); }}"
    )


def _error_code(label, seed):
    errc = ["invalid_argument", "no_such_file_or_directory", "permission_denied", "timed_out"][seed % 4]
    ec = f"std::make_error_code(std::errc::{errc})"
    return f'microfmt::format_to(out, TB_FMT("{label} last error: {{}}\\n"), {ec});'


def _std_optional_present(label, seed):
    return f'microfmt::format_to(out, TB_FMT("{label} std::optional (present)={{}}\\n"), std::optional<int>{{{seed}}});'


def _std_optional_absent(label, seed):
    del seed
    return f'microfmt::format_to(out, TB_FMT("{label} std::optional (absent)={{}}\\n"), std::optional<int>{{}});'


def _reloco_optional_present(label, seed):
    return f'microfmt::format_to(out, TB_FMT("{label} reloco::optional (present)={{}}\\n"), reloco::optional<int>{{{seed}}});'


def _reloco_optional_absent(label, seed):
    del seed
    return f'microfmt::format_to(out, TB_FMT("{label} reloco::optional (absent)={{}}\\n"), reloco::optional<int>{{}});'


def _reloco_expected_ok(label, seed):
    p = f"testbed::point3{{{seed}, {seed + 1}, {seed + 2}}}"
    return (
        f"{{ const microfmt::expected<testbed::point3, std::error_code> result{{{p}}}; "
        f'microfmt::format_to(out, TB_FMT("{label} expected (ok)={{}}\\n"), result); }}'
    )


def _reloco_expected_err(label, seed):
    errc = ["invalid_argument", "no_such_file_or_directory", "permission_denied", "timed_out"][seed % 4]
    return (
        f"{{ const microfmt::expected<testbed::point3, std::error_code> result{{"
        f"microfmt::unexpected(std::make_error_code(std::errc::{errc}))}}; "
        f'microfmt::format_to(out, TB_FMT("{label} expected (err)={{}}\\n"), result); }}'
    )


def _std_vector_join(label, seed):
    values = ", ".join(str(seed + i) for i in range(4))
    return (
        f"{{ const std::vector<int> samples{{{values}}}; "
        f'microfmt::format_to(out, TB_FMT("{label} std::vector samples=[{{}}]\\n"), microfmt::join(samples)); }}'
    )


def _std_array_join(label, seed):
    values = ", ".join(str(seed - i) for i in range(4))
    return (
        f"{{ const std::array<int, 4> samples{{{{{values}}}}}; "
        f'microfmt::format_to(out, TB_FMT("{label} std::array samples=[{{}}]\\n"), microfmt::join(samples)); }}'
    )


def _reloco_vector(label, seed):
    values = [seed + i * 3 for i in range(3)]
    pushes = " ".join(f"(void)vec.try_push_back({v});" for v in values)
    return (
        f"{{ reloco::vector<int> vec; {pushes} "
        f'microfmt::format_to(out, TB_FMT("{label} reloco::vector samples={{}}\\n"), vec); }}'
    )


def _reloco_flat_map(label, seed):
    inserts = " ".join(f"(void)readings.try_insert({i}, {seed + i * 11});" for i in range(3))
    return (
        f"{{ reloco::flat_map<int, int> readings; {inserts} "
        f'microfmt::format_to(out, TB_FMT("{label} reloco::flat_map readings={{}}\\n"), readings); }}'
    )


def _reloco_non_zero(label, seed):
    device_id = (seed % 4095) + 1
    return (
        f"{{ const auto id = reloco::non_zero<std::uint16_t>::try_create({device_id}); "
        f'microfmt::format_to(out, TB_FMT("{label} device id={{}}\\n"), id.value()); }}'
    )

def _reloco_arith_wrappers(label, seed):
    checked_v = seed % 100
    saturating_v = 200 + (seed % 55)
    wrapping_v = 200 + (seed % 55)
    return (
        f"{{ const reloco::checked<int> checked_value({checked_v}); "
        f"const reloco::saturating<std::uint8_t> saturating_value({saturating_v}); "
        f"const reloco::wrapping<std::uint8_t> wrapping_value({wrapping_v}); "
        f'microfmt::format_to(out, TB_FMT("{label} checked={{}} saturating={{}} wrapping={{}}\\n"), '
        f"checked_value, saturating_value, wrapping_value); }}"
    )


def _reloco_ordering(label, seed):
    order = ["less", "equal", "greater"][seed % 3]
    return f'microfmt::format_to(out, TB_FMT("{label} ordering={{}}\\n"), reloco::ordering::{order});'


def _std_string(label, seed):
    name = f"node-{seed % 64:02x}"
    return (
        f'{{ const std::string name("{name}"); '
        f'microfmt::format_to(out, TB_FMT("{label} std::string name={{}}\\n"), name); }}'
    )


def _std_string_view(label, seed):
    task = f"task-{seed % 32}"
    prio = seed % 10
    return f'microfmt::format_to(out, TB_FMT("{label} task \'{{}}\' priority={{}}\\n"), std::string_view("{task}"), {prio});'


def _reloco_sso_string(label, seed):
    name = f"dev-{seed % 64:02x}"
    return (
        f'{{ const auto created = reloco::sso_string::try_create(reloco::string_view("{name}")); '
        f'microfmt::format_to(out, TB_FMT("{label} reloco::sso_string name={{}}\\n"), created.value()); }}'
    )


def _semver(label, seed):
    major = seed % 10
    minor = (seed * 3) % 20
    patch = (seed * 7) % 50
    return (
        f'microfmt::format_to(out, TB_FMT("{label} version={{}}\\n"), '
        f"microfmt::version({major}, {minor}, {patch}, \"rc.1\"));"
    )


def _styled(label, seed):
    width = 10 + (seed % 8)
    return (
        f'microfmt::format_to(out, TB_FMT("{label} styled=[{{:^{width}}}]\\n"), '
        f'microfmt::pad("node-{seed % 64}", {width}));'
    )


def _raw_ptr(label, seed):
    addr = (seed * 0x1000 + 0x400000) & 0xFFFFFFFF
    return f'microfmt::format_to(out, TB_FMT("{label} ptr={{}}\\n"), microfmt::raw_ptr(reinterpret_cast<void *>(std::uintptr_t{{{addr}}})));'


def _escaped(label, seed):
    text = f"line-{seed % 32}\\ttab\\n"
    return f'microfmt::format_to(out, TB_FMT("{label} escaped={{}}\\n"), microfmt::escaped(microfmt::string_view("{text}", sizeof("{text}") - 1)));'


def _bitfield(label, seed):
    raw = (seed * 2654435761) & 0xFF
    return (
        f"{{ static constexpr microfmt::bit_field fields[] = {{"
        f'{{0x01u, 0, "READY", microfmt::bit_type::flag}}, '
        f'{{0x02u, 1, "ERROR", microfmt::bit_type::flag}}, '
        f'{{0xF0u, 4, "MODE", microfmt::bit_type::value_hex}}}}; '
        f'microfmt::format_to(out, TB_FMT("{label} bits={{}}\\n"), '
        f"microfmt::bits(std::uint32_t{{{raw}u}}, microfmt::span<const microfmt::bit_field>(fields, 3))); }}"
    )


def _uuid(label, seed):
    bytes_init = ", ".join(str((seed + i * 13) & 0xFF) for i in range(16))
    return (
        f"{{ static constexpr std::uint8_t id[16] = {{{bytes_init}}}; "
        f'microfmt::format_to(out, TB_FMT("{label} uuid={{}}\\n"), '
        f"microfmt::uuid(id)); }}"
    )


# Fixed catalog of argument-type combinations ("shapes"). Its *size* stays
# constant regardless of --count/--shapes-per-module: growing the module
# count adds more *call sites* (and thus more per-call-site format-string
# .rodata + one microfmt::format_to<Args...> call each), not more distinct
# Args... template instantiations.
SHAPE_CATALOG = [
    _static,
    _int,
    _int2,
    _hex_u32,
    _point,
    _hexdump,
    _error_code,
    _std_optional_present,
    _std_optional_absent,
    _reloco_optional_present,
    _reloco_optional_absent,
    _reloco_expected_ok,
    _reloco_expected_err,
    _std_vector_join,
    _std_array_join,
    _reloco_vector,
    _reloco_flat_map,
    _reloco_non_zero,
    _reloco_arith_wrappers,
    _reloco_ordering,
    _std_string,
    _std_string_view,
    _reloco_sso_string,
    _semver,
    _styled,
    _raw_ptr,
    _escaped,
    _bitfield,
    _uuid,
]

MODULE_FILE_TEMPLATE = """\
// Generated by tools/codesize/testbed/generate_modules.py -- do not edit.
#include "common.hpp"

namespace testbed {{

void run_module_{idx:03d}(out_t out) {{
{body}
}}

}} // namespace testbed
"""


def indent(lines):
    return textwrap.indent("\n".join(lines), "  ")


def generate(out_dir, count, shapes_per_module):
    modules_dir = os.path.join(out_dir, "modules")
    os.makedirs(modules_dir, exist_ok=True)

    for m in range(count):
        lines = []
        for k in range(shapes_per_module):
            shape_fn = SHAPE_CATALOG[(m * shapes_per_module + k) % len(SHAPE_CATALOG)]
            label = f"m{m:03d}s{k:02d}"
            seed = m * 31 + k * 5 + 1
            lines.append(shape_fn(label, seed))

        content = MODULE_FILE_TEMPLATE.format(idx=m, body=indent(lines))
        path = os.path.join(modules_dir, f"module_{m:03d}.cpp")
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)

    # modules.hpp: declarations shared by main.cpp / lib.cpp / all_modules.cpp.
    with open(os.path.join(out_dir, "modules.hpp"), "w", encoding="utf-8") as f:
        f.write("// Generated by tools/codesize/testbed/generate_modules.py -- do not edit.\n")
        f.write('#pragma once\n\n#include "common.hpp"\n\nnamespace testbed {\n\n')
        for m in range(count):
            f.write(f"void run_module_{m:03d}(out_t out);\n")
        f.write("\nvoid run_all_modules(out_t out);\n\n} // namespace testbed\n")

    # all_modules.cpp: one place calling every generated module, in order.
    with open(os.path.join(out_dir, "all_modules.cpp"), "w", encoding="utf-8") as f:
        f.write("// Generated by tools/codesize/testbed/generate_modules.py -- do not edit.\n")
        f.write('#include "modules.hpp"\n\nnamespace testbed {\n\nvoid run_all_modules(out_t out) {\n')
        for m in range(count):
            f.write(f"  run_module_{m:03d}(out);\n")
        f.write("}\n\n} // namespace testbed\n")

    return count


def generate_shards(out_dir, count, num_shards):
    """Partitions module_000..module_{count-1} into `num_shards` contiguous
    groups, each simulating one plugin/feature shared library that
    independently `#include`s microfmt/reloco headers (and so gets its own
    copy of every weak/inline symbol those headers instantiate -- the
    scenario run.sh's `multiso` mode measures cross-DSO duplication of).

    For shard i, emits shards/lib_{i:02d}.cpp:
        extern "C" void testbed_run_shard_{i:02d}();
    which calls that shard's module_NNN.cpp functions, and returns the list
    of (shard_index, [module_indices]) so run.sh knows which module_*.cpp
    files to compile into each .so.
    """
    shards_dir = os.path.join(out_dir, "shards")
    os.makedirs(shards_dir, exist_ok=True)

    shard_modules = [[] for _ in range(num_shards)]
    for m in range(count):
        shard_modules[m % num_shards].append(m)

    for i, module_indices in enumerate(shard_modules):
        path = os.path.join(shards_dir, f"lib_{i:02d}.cpp")
        with open(path, "w", encoding="utf-8") as f:
            f.write("// Generated by tools/codesize/testbed/generate_modules.py -- do not edit.\n")
            f.write('#include "../modules.hpp"\n\n')
            f.write(f'extern "C" void testbed_run_shard_{i:02d}() {{\n')
            f.write("  testbed::out_t out = testbed::backend_out();\n")
            for m in module_indices:
                f.write(f"  testbed::run_module_{m:03d}(out);\n")
            f.write("}\n")

    # main_multiso.cpp: calls every shard's exported entry point in turn, so
    # linking it against every generated libtestbed_shard_NN.so reproduces a
    # "one main app + many plugin .so's, each pulling in microfmt/reloco"
    # deployment.
    with open(os.path.join(out_dir, "main_multiso.cpp"), "w", encoding="utf-8") as f:
        f.write("// Generated by tools/codesize/testbed/generate_modules.py -- do not edit.\n")
        for i in range(num_shards):
            f.write(f'extern "C" void testbed_run_shard_{i:02d}();\n')
        f.write("\nint main() {\n")
        for i in range(num_shards):
            f.write(f"  testbed_run_shard_{i:02d}();\n")
        f.write("  return 0;\n}\n")

    return shard_modules


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", required=True, help="Directory to (re)generate modules/, modules.hpp, all_modules.cpp in")
    parser.add_argument("--count", type=int, default=32, help="Number of module_NNN.cpp translation units to generate (default: 32)")
    parser.add_argument(
        "--shapes-per-module", type=int, default=len(SHAPE_CATALOG), help="Call sites per module (default: one full catalog pass)"
    )
    parser.add_argument(
        "--lib-count",
        type=int,
        default=0,
        help="Additionally partition modules into this many shards/lib_NN.cpp + a main_multiso.cpp, for run.sh's multiso mode (default: 0, disabled)",
    )
    args = parser.parse_args()

    generate(args.out_dir, args.count, args.shapes_per_module)
    print(f"generated {args.count} modules ({args.count * args.shapes_per_module} call sites) in {args.out_dir}")

    if args.lib_count > 0:
        shard_modules = generate_shards(args.out_dir, args.count, args.lib_count)
        sizes = ", ".join(str(len(mods)) for mods in shard_modules)
        print(f"generated {args.lib_count} shard(s) for multiso mode (module counts: {sizes})")


if __name__ == "__main__":
    main()
