#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
"""Measures ELF section/symbol size for the codesize testbed's binaries.

Wraps binutils `size`/`nm` (or a cross-toolchain's, via --size-bin/--nm-bin)
into three subcommands used by run.sh:

  sections BINARY               Print .text/.rodata/.data.rel.ro/.data/.bss/
                                 unwind (.eh_frame*, .gcc_except_table)
                                 totals for BINARY, one "key=value" per line.

  record --toolchain T --opt O --backend MODE --artifact {exe,so} BINARY
                                 Append one TSV row (same fields, plus the
                                 binary's path) to stdout; run.sh redirects
                                 this into a shared report file. MODE is the
                                 string mode ("runtime" or "static", see
                                 common.hpp's TESTBED_STATIC_STRINGS).

  table REPORT_TSV               Read a report file written by `record` and
                                 print a human-readable table grouped by
                                 (toolchain, opt, artifact), including each
                                 row's delta against that group's `runtime`
                                 string-mode baseline row.

Section totals are used (not `nm` symbol sums) as the headline numbers
because they stay accurate even for stripped binaries and don't depend on
debug-info-driven symbol visibility; `nm` is only used for the optional
top-symbols view below.

  topsymbols BINARY [--top N] [--filter SUBSTR]
                                 Print the largest N symbols (default 20),
                                 optionally restricted to demangled names
                                 containing SUBSTR (e.g. "microfmt" or
                                 "testbed"), split into TEXT and RODATA.

  multiso --main EXE [--common LIB] SHARD_SO...
                                 For a "one main app + N plugin .so's"
                                 system (see run.sh's multiso mode): print
                                 each artifact's section-size buckets, the
                                 system-wide total, and the TEXT/RODATA
                                 symbols duplicated *identically* across
                                 multiple shard .so's -- the concrete
                                 optimization targets in this scenario,
                                 since each shard independently
                                 `#include`s microfmt/reloco and the linker
                                 can only dedupe weak/inline symbols within
                                 one DSO, never across separate .so's.
                                 --common optionally names a shared base
                                 library (e.g. MICROFMT_SHARED_BUILD's
                                 libmicrofmt_shared_common.so) that shards
                                 link against instead of duplicating a
                                 subset of symbols themselves; it counts
                                 toward the system total but is excluded
                                 from the duplication scan. --json-out PATH
                                 additionally writes a small machine-
                                 readable summary (system total, duplicate
                                 count/wasted bytes) that `report` (below)
                                 reads.

  report --pair LABEL:NAIVE_JSON:SHARED_JSON [--pair ...] [--out PATH]
                                 Reads the `multiso --json-out` summaries
                                 for a naive/shared pair per category (see
                                 run.sh) and renders one Markdown table
                                 comparing system-total size and cross-.so
                                 duplication before/after, plus an
                                 all-categories total row.
"""
import argparse
import json
import subprocess
import sys

FIELDS = ["toolchain", "opt", "backend", "artifact", "text", "rodata", "relro", "data", "bss", "unwind", "total", "path"]


def section_sizes(size_bin, binary):
    out = subprocess.run([size_bin, "-A", "-d", binary], capture_output=True, text=True, check=True).stdout
    sizes = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith("."):
            try:
                sz = int(parts[1])
            except ValueError:
                continue
            sizes[parts[0]] = sizes.get(parts[0], 0) + sz
    return sizes


def bucket_sizes(sizes):
    buckets = {"text": 0, "rodata": 0, "relro": 0, "data": 0, "bss": 0, "unwind": 0, "other": 0}
    for name, sz in sizes.items():
        if name.startswith(".text"):
            buckets["text"] += sz
        elif name.startswith(".rodata"):
            buckets["rodata"] += sz
        elif name.startswith(".data.rel.ro"):
            buckets["relro"] += sz
        elif name.startswith(".data"):
            buckets["data"] += sz
        elif name.startswith(".bss") or name.startswith(".tbss"):
            buckets["bss"] += sz
        elif name in (".eh_frame", ".eh_frame_hdr", ".gcc_except_table", ".ARM.extab", ".ARM.exidx"):
            buckets["unwind"] += sz
        else:
            buckets["other"] += sz
    return buckets


def cmd_sections(args):
    buckets = bucket_sizes(section_sizes(args.size_bin, args.binary))
    total = sum(buckets.values())
    for key in ("text", "rodata", "relro", "data", "bss", "unwind", "other"):
        print(f"{key}={buckets[key]}")
    print(f"total={total}")


def cmd_record(args):
    buckets = bucket_sizes(section_sizes(args.size_bin, args.binary))
    total = sum(buckets.values())
    row = [
        args.toolchain,
        args.opt,
        args.backend,
        args.artifact,
        buckets["text"],
        buckets["rodata"],
        buckets["relro"],
        buckets["data"],
        buckets["bss"],
        buckets["unwind"],
        total,
        args.binary,
    ]
    print("\t".join(str(x) for x in row))


def _read_report(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) != len(FIELDS):
                continue
            record = dict(zip(FIELDS, parts))
            for key in ("text", "rodata", "relro", "data", "bss", "unwind", "total"):
                record[key] = int(record[key])
            rows.append(record)
    return rows


def cmd_table(args):
    rows = _read_report(args.report)
    if not rows:
        print("(no rows in report)", file=sys.stderr)
        return

    groups = {}
    for row in rows:
        key = (row["toolchain"], row["opt"], row["artifact"])
        groups.setdefault(key, []).append(row)

    header = f"{'toolchain':<16} {'opt':<8} {'artifact':<5} {'strings':<9} {'text':>9} {'rodata':>9} {'relro':>8} {'unwind':>8} {'total':>9} {'Δtext':>9} {'Δrodata':>9}"
    print(header)
    print("-" * len(header))
    for (toolchain, opt, artifact), group_rows in sorted(groups.items()):
        baseline = next((r for r in group_rows if r["backend"] == "runtime"), group_rows[0])
        for row in sorted(group_rows, key=lambda r: r["backend"]):
            dtext = row["text"] - baseline["text"]
            drodata = row["rodata"] - baseline["rodata"]
            print(
                f"{toolchain:<16} {opt:<8} {artifact:<5} {row['backend']:<9} "
                f"{row['text']:>9} {row['rodata']:>9} {row['relro']:>8} {row['unwind']:>8} {row['total']:>9} "
                f"{dtext:>+9} {drodata:>+9}"
            )
        print()


def read_code_symbols(nm_bin, binary):
    """Returns {name: size} for TEXT (T/t/W/w) and RODATA (R/r) symbols in
    `binary`, as reported by `nm -CS --size-sort`. Requires an unstripped
    binary; silently returns {} if nm fails (e.g. stripped input)."""
    out = subprocess.run([nm_bin, "-CS", "--size-sort", binary], capture_output=True, text=True)
    if out.returncode != 0:
        return {}
    symbols = {}
    for line in out.stdout.splitlines():
        parts = line.split(maxsplit=3)
        if len(parts) < 4:
            continue
        _addr_hex, size_hex, sym_type, name = parts
        if sym_type not in ("T", "t", "W", "w", "R", "r"):
            continue
        try:
            symbols[name] = int(size_hex, 16)
        except ValueError:
            continue
    return symbols


def cmd_topsymbols(args):
    out = subprocess.run([args.nm_bin, "-CS", "--size-sort", args.binary], capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stderr, file=sys.stderr)
        sys.exit(out.returncode)

    text_syms = []
    rodata_syms = []
    for line in out.stdout.splitlines():
        parts = line.split(maxsplit=3)
        if len(parts) < 4:
            continue
        addr_hex, size_hex, sym_type, name = parts
        if args.filter and args.filter not in name:
            continue
        try:
            size = int(size_hex, 16)
        except ValueError:
            continue
        if sym_type in ("T", "t", "W", "w"):
            text_syms.append((size, name))
        elif sym_type in ("R", "r"):
            rodata_syms.append((size, name))

    def show(title, syms):
        print(f"=== {title} (top {args.top}) ===")
        for size, name in sorted(syms, reverse=True)[: args.top]:
            print(f"{size:>8}  {name}")
        print()

    show("TEXT", text_syms)
    show("RODATA", rodata_syms)


def cmd_multiso(args):
    """Reports a "main app + N plugin .so's" system's total footprint, and
    -- the actual optimization signal -- which TEXT/RODATA symbols (almost
    always microfmt::format_to<Args...>/formatter<T> template instantiations
    or reloco container/wrapper methods) are duplicated *identically* across
    multiple shard .so's, because each shard independently `#include`s
    microfmt/reloco and so gets its own weak-linkage copy that the linker
    can only dedupe *within* one DSO, never across separate .so's.

    --common optionally names a shared base library (e.g. one built with
    MICROFMT_SHARED_BUILD, see run.sh's "multiso-shared" mode and
    shared_common.cpp) that every shard links against instead of
    duplicating a subset of symbols itself: it is included in the system
    total like any other artifact, but excluded from the shard-duplication
    scan below, since by design it is the single copy the shards import
    rather than a duplicate of one."""
    artifacts = [("main", args.main)]
    if args.common:
        artifacts.append(("common", args.common))
    artifacts += [(f"shard_{i:02d}", p) for i, p in enumerate(args.shards)]
    totals = {"text": 0, "rodata": 0, "relro": 0, "data": 0, "bss": 0, "unwind": 0, "other": 0}

    header = f"{'artifact':<12} {'text':>9} {'rodata':>9} {'relro':>8} {'unwind':>8} {'total':>9}"
    print(header)
    print("-" * len(header))
    for name, path in artifacts:
        buckets = bucket_sizes(section_sizes(args.size_bin, path))
        artifact_total = sum(buckets.values())
        for key in totals:
            totals[key] += buckets[key]
        print(
            f"{name:<12} {buckets['text']:>9} {buckets['rodata']:>9} {buckets['relro']:>8} "
            f"{buckets['unwind']:>8} {artifact_total:>9}"
        )
    grand_total = sum(totals.values())
    print("-" * len(header))
    print(
        f"{'SYSTEM TOTAL':<12} {totals['text']:>9} {totals['rodata']:>9} {totals['relro']:>8} "
        f"{totals['unwind']:>8} {grand_total:>9}"
    )

    occurrences = {}
    for i, path in enumerate(args.shards):
        for name, size in read_code_symbols(args.nm_bin, path).items():
            occurrences.setdefault(name, []).append((f"shard_{i:02d}", size))

    def wasted(occ):
        sizes = [s for _, s in occ]
        # Every copy past the first is pure duplication; the linker could
        # only have kept one (e.g. by moving the symbol to a shared,
        # `-shared`-linked-against-by-all base library).
        return sum(sizes) - max(sizes)

    dupes = {name: occ for name, occ in occurrences.items() if len(occ) > 1}
    ranked = sorted(dupes.items(), key=lambda kv: wasted(kv[1]), reverse=True)
    total_wasted = sum(wasted(occ) for _, occ in ranked)

    print()
    print(
        f"==> {len(ranked)} symbol(s) duplicated across {len(args.shards)} shard .so's; "
        f"{total_wasted} bytes of redundant TEXT/RODATA (top {args.top}):"
    )
    print(f"{'wasted':>8} {'copies':>7} {'size':>7}  name")
    for name, occ in ranked[: args.top]:
        sizes = [s for _, s in occ]
        print(f"{wasted(occ):>8} {len(occ):>7} {max(sizes):>7}  {name}")

    if args.json_out:
        summary = {
            "totals": totals,
            "grand_total": grand_total,
            "shard_count": len(args.shards),
            "has_common": args.common is not None,
            "duplicate_symbol_count": len(ranked),
            "duplicate_wasted_bytes": total_wasted,
        }
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump(summary, f)


def cmd_report(args):
    """Reads the {naive,shared} `multiso --json-out` summaries `run.sh`
    writes for every (toolchain, opt, mode) category and renders one
    Markdown table comparing them: system-total bytes before/after the
    MICROFMT_SHARED/RELOCO_SHARED common .so, the byte/percentage
    reduction, and how much of that came from actually eliminating
    cross-.so duplication (vs. simply moving bytes into the one now-shared
    common .so) -- see run.sh's "multiso" mode and README.md for what each
    column measures."""
    rows = []
    for entry in args.pairs:
        try:
            label, naive_path, shared_path = entry.split(":", 2)
        except ValueError:
            print(f"--pair must be LABEL:NAIVE_JSON:SHARED_JSON, got: {entry!r}", file=sys.stderr)
            sys.exit(2)
        with open(naive_path, encoding="utf-8") as f:
            naive = json.load(f)
        with open(shared_path, encoding="utf-8") as f:
            shared = json.load(f)
        rows.append((label, naive, shared))

    lines = []
    lines.append(f"# {args.title}" if args.title else "# Code-size testbed: multiso duplication report")
    lines.append("")
    lines.append(
        "One row per (toolchain, optimization level, string mode) category: `naive` independently "
        "instantiates microfmt/reloco in every shard `.so`; `shared` links every shard against one common "
        "`.so` built with `MICROFMT_SHARED_BUILD`/`RELOCO_SHARED_BUILD` instead (see run.sh and "
        "docs/shared-library.md)."
    )
    lines.append("")
    lines.append(
        "| category | naive total | shared total | reduction | reduction % | duplicated bytes eliminated |"
    )
    lines.append("|---|---:|---:|---:|---:|---:|")

    total_naive = 0
    total_shared = 0
    for label, naive, shared in rows:
        naive_total = naive["grand_total"]
        shared_total = shared["grand_total"]
        reduction = naive_total - shared_total
        pct = (reduction / naive_total * 100.0) if naive_total else 0.0
        dupe_before = naive["duplicate_wasted_bytes"]
        dupe_after = shared["duplicate_wasted_bytes"]
        dupe_eliminated = dupe_before - dupe_after
        total_naive += naive_total
        total_shared += shared_total
        lines.append(
            f"| {label} | {naive_total:,} B | {shared_total:,} B | {reduction:,} B | {pct:.1f}% | "
            f"{dupe_eliminated:,} B ({dupe_before:,} \u2192 {dupe_after:,}) |"
        )

    if len(rows) > 1:
        total_reduction = total_naive - total_shared
        total_pct = (total_reduction / total_naive * 100.0) if total_naive else 0.0
        lines.append(
            f"| **all categories** | {total_naive:,} B | {total_shared:,} B | {total_reduction:,} B | "
            f"{total_pct:.1f}% | |"
        )

    report = "\n".join(lines) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(report)
    else:
        print(report, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size-bin", default="size", help="size(1) binary to use (default: size)")
    parser.add_argument("--nm-bin", default="nm", help="nm(1) binary to use (default: nm)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_sections = sub.add_parser("sections", help="Print section-size buckets for one binary")
    p_sections.add_argument("binary")
    p_sections.set_defaults(func=cmd_sections)

    p_record = sub.add_parser("record", help="Append one TSV row describing one binary's section sizes")
    p_record.add_argument("--toolchain", required=True)
    p_record.add_argument("--opt", required=True)
    p_record.add_argument("--backend", required=True)
    p_record.add_argument("--artifact", required=True, choices=["exe", "so"])
    p_record.add_argument("binary")
    p_record.set_defaults(func=cmd_record)

    p_table = sub.add_parser("table", help="Pretty-print a report file written by `record`")
    p_table.add_argument("report")
    p_table.set_defaults(func=cmd_table)

    p_top = sub.add_parser("topsymbols", help="List the largest TEXT/RODATA symbols in one binary")
    p_top.add_argument("binary")
    p_top.add_argument("--top", type=int, default=20)
    p_top.add_argument("--filter", default=None)
    p_top.set_defaults(func=cmd_topsymbols)

    p_multiso = sub.add_parser(
        "multiso", help="Report a main-app + N plugin-.so system's total footprint and cross-.so symbol duplication"
    )
    p_multiso.add_argument("--main", required=True, help="Path to the main executable")
    p_multiso.add_argument("--common", default=None, help="Path to an optional shared base library (see shared_common.cpp)")
    p_multiso.add_argument("shards", nargs="+", help="Paths to each plugin/feature .so")
    p_multiso.add_argument("--top", type=int, default=25)
    p_multiso.add_argument("--json-out", dest="json_out", default=None, help="Write a machine-readable summary JSON to this path")
    p_multiso.set_defaults(func=cmd_multiso)

    p_report = sub.add_parser(
        "report", help="Render a Markdown naive-vs-shared reduction table from multiso --json-out summaries"
    )
    p_report.add_argument(
        "--pair",
        dest="pairs",
        action="append",
        required=True,
        help="LABEL:NAIVE_JSON:SHARED_JSON; repeat once per (toolchain, opt, mode) category",
    )
    p_report.add_argument("--title", default=None)
    p_report.add_argument("--out", default=None, help="Write Markdown to this path instead of stdout")
    p_report.set_defaults(func=cmd_report)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
