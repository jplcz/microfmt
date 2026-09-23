#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause
"""Codesize testbed orchestrator.

Pure-Python replacement for the previous hand-rolled bash background-job
pool: builds an in-memory dependency graph of every compile/link/measure
step the testbed needs (mirroring run.sh's old build_category /
build_common / build_naive_variant / build_shared_variant recipes 1:1,
byte-for-byte the same compiler invocations) and runs it with a bounded
thread pool, scheduling each task the instant its own dependencies are
done rather than waiting on an entire phase/category to finish.

Every compiler/linker/measure.py invocation is executed via
subprocess.run() with an explicit argv list -- never through a shell -- so
there is no quoting/escaping to get right (the same benefit CMake's
add_custom_command gets from not running COMMAND through a shell), and no
extra external build-system dependency is introduced: only python3, which
this testbed already requires for generate_modules.py and measure.py.

Usage: tools/codesize/testbed/orchestrate.py [--keep-build]
(normally invoked via run.sh, which is now a thin wrapper around this
script -- see run.sh for the full list of supported environment
variables, reproduced here for reference):

  TESTBED_TOOLCHAINS     Space-separated list of C++ compilers to try.
                         Default: "g++ clang++". Missing ones are skipped.
  TESTBED_OPT_LEVELS     Space-separated list of optimization flags.
                         Default: "-O0 -Os -O2 -O3".
  TESTBED_STRING_MODES   Space-separated subset of "runtime static".
                         Default: "runtime static".
  TESTBED_MODULE_COUNT   Number of generated modules/module_NNN.cpp files.
                         Default: 32.
  TESTBED_RUN_MULTISO    "1" (default) or "0". See run.sh's header comment
                         for what multiso mode measures.
  TESTBED_NUM_SO         Number of shard .so's for multiso mode.
                         Default: 4.
  MICROFMT_RELOCO_INCLUDE_DIR
                         Path to a reloco checkout's include/ directory.
                         Default: a sibling ../reloco checkout next to
                         this repository.
  TESTBED_JOBS           Max concurrently-running tasks. Default: the
                         number of CPUs (os.cpu_count(), or 4).

Parallelism: every compile, link, sanity-check run, and measure.py
invocation is its own task node with explicit dependencies on the exact
artifacts it needs (not just "the same category" as before) -- e.g. the
naive and shared variants of a given (toolchain, opt, mode), or the exe
and .so within one category, now run concurrently with each other too,
not just across categories. Task output is captured (not streamed) and
printed under a lock as soon as each task finishes, in true completion
order -- concurrent tasks' output is never interleaved mid-line, but
unlike the old fixed-order replay, the terminal now shows genuine
real-time progress.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = (SCRIPT_DIR / ".." / ".." / "..").resolve()
BUILD_DIR = SCRIPT_DIR / "build"

# Deliberately *outside* BUILD_DIR (see main()'s cleanup): BUILD_DIR is
# wiped at exit unless --keep-build, but the Markdown reduction report is
# the one human-readable artifact worth keeping from every run regardless,
# so it always gets written here and is never deleted.
CODESIZE_REPORT = SCRIPT_DIR / "codesize-report.md"


def _env_list(name: str, default: str) -> list[str]:
    return os.environ.get(name, default).split()


def strip_leading_dash(value: str) -> str:
    """Mirrors bash's ``${opt#-}``: removes exactly one leading '-'."""
    return value[1:] if value.startswith("-") else value


def binutils_prefix_for(cxx: str) -> str:
    """Picks the binutils `size`/`nm` prefix matching a given compiler, so
    cross toolchains (e.g. aarch64-linux-gnu-g++) are measured with their
    own target-appropriate tools rather than the host's."""
    if cxx.endswith("-g++") or cxx.endswith("-clang++"):
        return cxx.rsplit("-", 1)[0] + "-"
    return ""


def string_mode_defines(mode: str) -> list[str]:
    defines = [f'-DTESTBED_STRING_MODE_LABEL="{mode}"']
    if mode == "static":
        defines.append("-DTESTBED_STATIC_STRINGS=1")
    return defines


# --------------------------------------------------------------------------
# Task graph: a small dependency-respecting executor. Each Task is either an
# argv subprocess invocation (compiles, links, measure.py calls) or a plain
# Python callable (the exe sanity-check run) -- both run inside the thread
# pool exactly the same way, and both fail the same way (nonzero return /
# raised exception), which marks any dependent task "skipped" instead of
# hard-aborting the whole run the instant one task fails.
# --------------------------------------------------------------------------


@dataclass
class Task:
    name: str
    depends_on: tuple[str, ...] = ()
    argv: Optional[list[str]] = None
    func: Optional[Callable[[], str]] = None
    label: str = ""  # short human-readable status line; defaults to name
    ok: bool = False
    output: str = ""
    duration: float = 0.0

    def __post_init__(self) -> None:
        if not self.label:
            self.label = self.name


class Graph:
    def __init__(self, max_workers: int) -> None:
        self.tasks: dict[str, Task] = {}
        self.max_workers = max_workers
        self._print_lock = threading.Lock()

    def add(self, task: Task) -> Task:
        assert task.name not in self.tasks, f"duplicate task name: {task.name}"
        self.tasks[task.name] = task
        return task

    def _execute(self, task: Task, deps_ok: bool) -> Task:
        if not deps_ok:
            task.ok = False
            task.output = "skipped: a dependency failed"
            return task

        start = time.monotonic()
        try:
            if task.argv is not None:
                proc = subprocess.run(task.argv, capture_output=True, text=True)
                task.output = (proc.stdout or "") + (proc.stderr or "")
                task.ok = proc.returncode == 0
            elif task.func is not None:
                task.output = task.func()
                task.ok = True
            else:
                task.ok = True
        except Exception as exc:  # noqa: BLE001 -- surfaced via task.ok/output
            task.output = (task.output + f"\n{exc}").strip()
            task.ok = False
        task.duration = time.monotonic() - start

        with self._print_lock:
            status = "ok" if task.ok else "FAIL"
            print(f"==> [{status}] {task.label} ({task.duration:.1f}s)")
            if not task.ok and task.output:
                print(task.output.rstrip("\n"))
        return task

    def run(self) -> bool:
        """Runs every task, respecting dependencies, bounded by
        max_workers concurrent tasks. Returns True iff every task
        succeeded."""
        remaining = dict(self.tasks)
        done: dict[str, bool] = {}
        in_flight: dict[str, concurrent.futures.Future] = {}

        with concurrent.futures.ThreadPoolExecutor(max_workers=self.max_workers) as pool:
            while remaining or in_flight:
                ready = [
                    t
                    for name, t in remaining.items()
                    if all(d in done for d in t.depends_on)
                ]
                for t in ready:
                    del remaining[t.name]
                    deps_ok = all(done[d] for d in t.depends_on)
                    in_flight[t.name] = pool.submit(self._execute, t, deps_ok)

                if not in_flight:
                    raise RuntimeError(
                        f"deadlock: unresolved task dependencies for {sorted(remaining)}"
                    )

                completed, _ = concurrent.futures.wait(
                    in_flight.values(), return_when=concurrent.futures.FIRST_COMPLETED
                )
                for name in [n for n, fut in in_flight.items() if fut in completed]:
                    task = in_flight.pop(name).result()
                    done[name] = task.ok

        return all(done.values())


# --------------------------------------------------------------------------
# Graph construction
# --------------------------------------------------------------------------


def build_graph(
    graph: Graph,
    *,
    toolchains: list[str],
    opt_levels: list[str],
    string_modes: list[str],
    module_count: int,
    run_multiso: bool,
    num_so: int,
    reloco_include_dir: Path,
    gen_dir: Path,
    report_rows: list[str],
    rows_lock: threading.Lock,
    report_pairs: list[tuple[str, Path, Path]],
) -> None:
    includes = [
        "-I", str(REPO_ROOT / "include"),
        "-I", str(reloco_include_dir),
        "-I", str(SCRIPT_DIR),
        "-I", str(gen_dir),
    ]
    common_sources = [str(SCRIPT_DIR / "sink.cpp"), str(gen_dir / "all_modules.cpp")] + sorted(
        str(p) for p in gen_dir.glob("modules/module_*.cpp")
    )

    available_toolchains = []
    for cxx in toolchains:
        if shutil.which(cxx):
            available_toolchains.append(cxx)
        else:
            print(f"==> skipping unavailable toolchain: {cxx}", file=sys.stderr)

    if not available_toolchains:
        print(
            f"error: none of the requested toolchains ({' '.join(toolchains)}) were found on PATH",
            file=sys.stderr,
        )
        sys.exit(1)

    def record_row_func(size_bin: str, cxx: str, opt: str, mode: str, artifact: str, path: Path) -> Callable[[], str]:
        def _run() -> str:
            proc = subprocess.run(
                [
                    sys.executable, str(SCRIPT_DIR / "measure.py"),
                    "--size-bin", size_bin,
                    "record",
                    "--toolchain", cxx,
                    f"--opt={opt}",
                    "--backend", mode,
                    "--artifact", artifact,
                    str(path),
                ],
                capture_output=True, text=True, check=True,
            )
            with rows_lock:
                report_rows.append(proc.stdout)
            return proc.stderr

        return _run

    # --- Main table: one (cxx, opt, mode) "category" per exe+.so pair. ---
    for cxx in available_toolchains:
        prefix = binutils_prefix_for(cxx)
        size_bin = prefix + "size" if shutil.which(prefix + "size") else "size"

        for opt in opt_levels:
            for mode in string_modes:
                label = f"{cxx}_{strip_leading_dash(opt)}_{mode}"
                out_dir = BUILD_DIR / label
                out_dir.mkdir(parents=True, exist_ok=True)
                defines = string_mode_defines(mode)

                exe_path = out_dir / "testbed_exe"
                so_path = out_dir / "libtestbed.so"

                compile_exe = graph.add(Task(
                    name=f"compile_exe_{label}",
                    argv=[cxx, "-std=c++20", opt, *includes, *defines,
                          *common_sources, str(SCRIPT_DIR / "main.cpp"), "-o", str(exe_path)],
                    label=f"{cxx} {opt} ({mode} strings): compiling exe",
                ))
                compile_so = graph.add(Task(
                    name=f"compile_so_{label}",
                    argv=[cxx, "-std=c++20", opt, "-fPIC", "-fvisibility=hidden", *includes, *defines,
                          "-shared", *common_sources, str(SCRIPT_DIR / "lib.cpp"), "-o", str(so_path)],
                    label=f"{cxx} {opt} ({mode} strings): compiling .so",
                ))
                graph.add(Task(
                    name=f"record_exe_{label}",
                    depends_on=(compile_exe.name,),
                    func=record_row_func(size_bin, cxx, opt, mode, "exe", exe_path),
                    label=f"{cxx} {opt} ({mode} strings): measuring exe",
                ))
                graph.add(Task(
                    name=f"record_so_{label}",
                    depends_on=(compile_so.name,),
                    func=record_row_func(size_bin, cxx, opt, mode, "so", so_path),
                    label=f"{cxx} {opt} ({mode} strings): measuring .so",
                ))

    if not run_multiso:
        return

    shard_count = len(list((gen_dir / "shards").glob("lib_*.cpp")))

    # Every module_*.cpp assigned to each shard (round-robin by index mod
    # shard_count, matching generate_modules.py's generate_shards).
    shard_modules_by_index: list[list[str]] = [[] for _ in range(shard_count)]
    for module_path in sorted(gen_dir.glob("modules/module_*.cpp")):
        module_idx = int(module_path.stem.removeprefix("module_"))
        shard_modules_by_index[module_idx % shard_count].append(str(module_path))

    def add_multiso_variant(
        *, cxx: str, opt: str, mode: str, size_bin: str, nm_bin: str,
        shared: bool, common_so: Optional[Path], common_task_name: Optional[str],
    ) -> tuple[str, Path]:
        defines = string_mode_defines(mode)
        variant = "shared" if shared else "naive"
        if shared:
            defines = [*defines, "-DMICROFMT_SHARED", "-DRELOCO_SHARED"]

        out_dir = BUILD_DIR / f"multiso_{'shared_' if shared else ''}{cxx}_{strip_leading_dash(opt)}_{mode}"
        out_dir.mkdir(parents=True, exist_ok=True)

        shard_tasks = []
        shard_paths = []
        for i in range(shard_count):
            shard_label = f"{i:02d}"
            shard_so = out_dir / f"libtestbed_shard_{shard_label}.so"
            argv = [cxx, "-std=c++20", opt, "-fPIC", *includes, *defines, "-shared",
                    str(SCRIPT_DIR / "sink.cpp"), *shard_modules_by_index[i],
                    str(gen_dir / "shards" / f"lib_{shard_label}.cpp"), "-o", str(shard_so)]
            deps: tuple[str, ...] = ()
            if shared:
                # Deliberately *not* -fvisibility=hidden in the naive
                # branch here: this is the naive baseline multi-.so build
                # every module_*.cpp independently #including
                # microfmt/reloco produces (see README.md for why that
                # flag alone doesn't fix the duplication this mode
                # reports).
                assert common_so is not None and common_task_name is not None
                argv += ["-L", str(common_so.parent), "-lmicrofmt_shared_common", "-Wl,-rpath," + str(common_so.parent)]
                deps = (common_task_name,)
            shard_task = graph.add(Task(
                name=f"multiso_{variant}_shard_{shard_label}_{cxx}_{strip_leading_dash(opt)}_{mode}",
                argv=argv,
                depends_on=deps,
                label=f"multiso ({variant}): {cxx} {opt} ({mode}) shard {shard_label}",
            ))
            shard_tasks.append(shard_task.name)
            shard_paths.append(shard_so)

        main_exe = out_dir / "testbed_main"
        main_argv = [cxx, "-std=c++20", opt, *includes, *defines,
                     str(gen_dir / "main_multiso.cpp"), *[str(p) for p in shard_paths]]
        if shared:
            assert common_so is not None
            main_argv += ["-L", str(common_so.parent), "-lmicrofmt_shared_common",
                          "-Wl,-rpath," + str(out_dir), "-Wl,-rpath," + str(common_so.parent)]
        else:
            main_argv += ["-Wl,-rpath," + str(out_dir)]
        main_argv += ["-o", str(main_exe)]

        link_task = graph.add(Task(
            name=f"multiso_{variant}_main_{cxx}_{strip_leading_dash(opt)}_{mode}",
            argv=main_argv,
            depends_on=tuple(shard_tasks),
            label=f"multiso ({variant}): {cxx} {opt} ({mode}) linking main",
        ))

        def _sanity_check(exe=main_exe) -> str:
            subprocess.run([str(exe)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            return ""

        runcheck_task = graph.add(Task(
            name=f"multiso_{variant}_runcheck_{cxx}_{strip_leading_dash(opt)}_{mode}",
            depends_on=(link_task.name,),
            func=_sanity_check,
            label=f"multiso ({variant}): {cxx} {opt} ({mode}) sanity-check run",
        ))

        summary_json = out_dir / "summary.json"
        measure_argv = [sys.executable, str(SCRIPT_DIR / "measure.py"),
                        "--size-bin", size_bin, "--nm-bin", nm_bin, "multiso",
                        "--main", str(main_exe)]
        if shared:
            assert common_so is not None
            measure_argv += ["--common", str(common_so)]
        measure_argv += [str(p) for p in shard_paths]
        measure_argv += ["--json-out", str(summary_json)]

        graph.add(Task(
            name=f"multiso_{variant}_measure_{cxx}_{strip_leading_dash(opt)}_{mode}",
            argv=measure_argv,
            depends_on=(runcheck_task.name,),
            label=f"multiso ({variant}): {cxx} {opt} ({mode}) measuring",
        ))

        return f"multiso_{variant}_measure_{cxx}_{strip_leading_dash(opt)}_{mode}", summary_json

    for cxx in available_toolchains:
        prefix = binutils_prefix_for(cxx)
        size_bin = prefix + "size" if shutil.which(prefix + "size") else "size"
        nm_bin = prefix + "nm" if shutil.which(prefix + "nm") else "nm"

        for opt in opt_levels:
            # Built once per (toolchain, opt): every (mode) below for the
            # same (cxx, opt) reuses this one .so -- see run.sh's original
            # comment on build_common for why it doesn't vary with mode.
            common_dir = BUILD_DIR / f"multiso_shared_common_{cxx}_{strip_leading_dash(opt)}"
            common_dir.mkdir(parents=True, exist_ok=True)
            common_so = common_dir / "libmicrofmt_shared_common.so"
            common_task = graph.add(Task(
                name=f"multiso_common_{cxx}_{strip_leading_dash(opt)}",
                argv=[cxx, "-std=c++20", opt, "-fPIC",
                      "-I", str(REPO_ROOT / "include"), "-I", str(reloco_include_dir),
                      "-shared", str(SCRIPT_DIR / "shared_common.cpp"), "-o", str(common_so)],
                label=f"multiso: {cxx} {opt} building common .so",
            ))

            for mode in string_modes:
                naive_measure_name, naive_json = add_multiso_variant(
                    cxx=cxx, opt=opt, mode=mode, size_bin=size_bin, nm_bin=nm_bin,
                    shared=False, common_so=None, common_task_name=None,
                )
                shared_measure_name, shared_json = add_multiso_variant(
                    cxx=cxx, opt=opt, mode=mode, size_bin=size_bin, nm_bin=nm_bin,
                    shared=True, common_so=common_so, common_task_name=common_task.name,
                )
                report_pairs.append((f"{cxx} {opt} ({mode})", naive_json, shared_json))


def main() -> int:
    # Several subprocess calls below (generate_modules.py, measure.py
    # table/report) inherit stdout directly rather than being captured, so
    # their writes reach the terminal immediately -- without this, Python's
    # own (block-)buffered print() calls interleaved around them can show
    # up *after* a later subprocess's output despite running first.
    sys.stdout.reconfigure(line_buffering=True)

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--keep-build", action="store_true",
                         help="Don't delete the build/ scratch directory on exit.")
    args = parser.parse_args()

    reloco_include_dir = Path(
        os.environ.get("MICROFMT_RELOCO_INCLUDE_DIR", str(REPO_ROOT / ".." / "reloco" / "include"))
    ).resolve()
    if not (reloco_include_dir / "reloco" / "vector.hpp").is_file():
        print(f"error: reloco headers not found under '{reloco_include_dir}'", file=sys.stderr)
        print("       set MICROFMT_RELOCO_INCLUDE_DIR to a reloco checkout's include/ directory", file=sys.stderr)
        return 1

    toolchains = _env_list("TESTBED_TOOLCHAINS", "g++ clang++")
    opt_levels = _env_list("TESTBED_OPT_LEVELS", "-O0 -Os -O2 -O3")
    string_modes = _env_list("TESTBED_STRING_MODES", "runtime static")
    module_count = int(os.environ.get("TESTBED_MODULE_COUNT", "32"))
    run_multiso = os.environ.get("TESTBED_RUN_MULTISO", "1") == "1"
    num_so = int(os.environ.get("TESTBED_NUM_SO", "4"))
    max_jobs = int(os.environ.get("TESTBED_JOBS", str(os.cpu_count() or 4)))

    shutil.rmtree(BUILD_DIR, ignore_errors=True)
    BUILD_DIR.mkdir(parents=True)

    try:
        gen_dir = BUILD_DIR / "generated"
        gen_args = [sys.executable, str(SCRIPT_DIR / "generate_modules.py"),
                    "--out-dir", str(gen_dir), "--count", str(module_count)]
        if run_multiso:
            gen_args += ["--lib-count", str(num_so)]
        subprocess.run(gen_args, check=True)

        report_rows: list[str] = []
        rows_lock = threading.Lock()
        report_pairs: list[tuple[str, Path, Path]] = []

        graph = Graph(max_workers=max_jobs)
        build_graph(
            graph,
            toolchains=toolchains, opt_levels=opt_levels, string_modes=string_modes,
            module_count=module_count, run_multiso=run_multiso, num_so=num_so,
            reloco_include_dir=reloco_include_dir, gen_dir=gen_dir,
            report_rows=report_rows, rows_lock=rows_lock, report_pairs=report_pairs,
        )

        ok = graph.run()

        report_path = BUILD_DIR / "report.tsv"
        report_path.write_text("".join(report_rows))

        print()
        print("==> Section-size report (bytes; Δ columns are vs the 'runtime' string mode in the same row group):")
        subprocess.run([sys.executable, str(SCRIPT_DIR / "measure.py"), "table", str(report_path)], check=False)

        if run_multiso:
            report_args = []
            for title, naive_json, shared_json in report_pairs:
                if not (naive_json.is_file() and shared_json.is_file()):
                    print(f"warning: skipping report row '{title}' (a build failed, no summary.json)", file=sys.stderr)
                    continue
                report_args += ["--pair", f"{title}:{naive_json}:{shared_json}"]

            if report_args:
                subprocess.run(
                    [sys.executable, str(SCRIPT_DIR / "measure.py"), "report", *report_args,
                     "--out", str(CODESIZE_REPORT)],
                    check=False,
                )
                print()
                print(f"Markdown reduction report: {CODESIZE_REPORT}")
                print(CODESIZE_REPORT.read_text(), end="")

        print()
        print(f"Full TSV report: {report_path}")
        if args.keep_build:
            print(f"Binaries kept under: {BUILD_DIR} (use 'measure.py topsymbols <binary>' to inspect one)")

        return 0 if ok else 1
    finally:
        if not args.keep_build:
            shutil.rmtree(BUILD_DIR, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
