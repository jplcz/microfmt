#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

"""
microfmt static stack usage analyzer
Parses GCC `.su` files and Clang `llvm-readelf --stack-sizes` output.
Enforces stack budgets strictly on microfmt symbols while benchmarking libc sprintf as a reference.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import List, Tuple

# ANSI terminal colors
COLOR_RED = "\033[31;1m"
COLOR_YELLOW = "\033[33;1m"
COLOR_GREEN = "\033[32m"
COLOR_CYAN = "\033[36m"
COLOR_BLUE = "\033[34;1m"
COLOR_RESET = "\033[0m"


def demangle_symbol(name: str) -> str:
    """Demangles C++ Itanium ABI symbols using llvm-cxxfilt or c++filt if installed."""
    if not name.startswith("_Z"):
        return name
    tool = shutil.which("llvm-cxxfilt") or shutil.which("c++filt")
    if not tool:
        return name
    try:
        proc = subprocess.run(
            [tool, "-p", name], capture_output=True, text=True, check=True
        )
        return proc.stdout.strip()
    except Exception:
        return name


def is_reference_symbol(func_name: str) -> bool:
    """Returns True if the function is a libc/snprintf baseline comparison."""
    lower = func_name.lower()
    return "probe_libc" in lower or "snprintf" in lower or "sprintf" in lower

def parse_gcc_su_file(su_path: str) -> List[Tuple[str, int, str]]:
    results = []
    if not os.path.exists(su_path):
        return results

    # Matches: /path/to/file.cpp:123:45:optional_qualifiers\t<size>\t<type>
    # Group 1: Function name / mangled symbol / template signature
    # Group 2: Stack size in bytes
    # Group 3: Allocation type (static, dynamic, etc.)
    entry_pattern = re.compile(
        r"^.+?:\d+:\d+:(?:[a-zA-Z0-9_]+:)?\s*(.*?)\t+(\d+)\t+([a-zA-Z0-9_]+)"
    )

    with open(su_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            match = entry_pattern.match(line)
            if match:
                func_name, size_str, alloc_type = match.groups()
                func_name = func_name.strip()
                if not func_name:
                    continue
                try:
                    results.append((func_name, int(size_str), alloc_type))
                except ValueError:
                    continue
            else:
                # Fallback tab-split if regex doesn't match
                parts = line.split("\t")
                if len(parts) >= 2:
                    loc_func = parts[0]
                    # Strip leading file:line:col:
                    cleaned_name = re.sub(r"^.+?:\d+:\d+:", "", loc_func).strip()
                    try:
                        results.append((cleaned_name, int(parts[1]), parts[2] if len(parts) > 2 else "static"))
                    except ValueError:
                        continue

    return results


def parse_clang_readelf(obj_path: str, readelf_bin: str) -> List[Tuple[str, int, str]]:
    results = []
    if not os.path.exists(obj_path):
        return results

    try:
        proc = subprocess.run(
            [readelf_bin, "--stack-sizes", obj_path],
            capture_output=True,
            text=True,
            check=True,
        )
    except (subprocess.SubprocessError, FileNotFoundError) as e:
        print(f"Error running {readelf_bin}: {e}", file=sys.stderr)
        return results

    pattern = re.compile(r"^\s*([0-9a-fA-Fx]+)\s+(.+)$")
    in_table = False

    for line in proc.stdout.splitlines():
        line = line.strip()
        if "Stack Sizes:" in line or ("Size" in line and "Function" in line):
            in_table = True
            continue
        if not in_table or not line:
            continue

        match = pattern.match(line)
        if match:
            raw_size, func_name = match.groups()
            try:
                size = int(raw_size, 0)
                results.append((func_name.strip(), size, "static"))
            except ValueError:
                continue

    return results


def collect_target_files(args: argparse.Namespace) -> List[str]:
    """Resolves target input files from either --input or --input-dir."""
    if args.input:
        return [args.input] if os.path.exists(args.input) else []

    search_dir = Path(args.input_dir)
    if not search_dir.is_dir():
        print(f"{COLOR_RED}[ERROR] Input directory not found: {args.input_dir}{COLOR_RESET}", file=sys.stderr)
        return []

    if args.compiler == "gcc":
        # Matches stack_probes.cpp.su, stack_probes.su, etc.
        return [str(p) for p in search_dir.rglob("*.su")]
    else:
        # Matches object files (.o, .obj) containing .stack_sizes metadata
        return [str(p) for p in search_dir.rglob("*.o")] + [str(p) for p in search_dir.rglob("*.obj")]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Static Stack Usage Analyzer for microfmt"
    )
    parser.add_argument(
        "--compiler", choices=["gcc", "clang"], required=True, help="Compiler flavor"
    )

    input_group = parser.add_mutually_exclusive_group(required=True)
    input_group.add_argument(
        "--input",
        help="Path to single .su file (GCC) or .o object file (Clang)",
    )
    input_group.add_argument(
        "--input-dir",
        help="Directory to recursively search for .su files (GCC) or .o files (Clang)",
    )

    parser.add_argument(
        "--readelf",
        default="llvm-readelf",
        help="Path to llvm-readelf / readelf binary (Clang only)",
    )
    parser.add_argument(
        "--budget",
        type=int,
        default=256,
        help="Maximum stack frame budget in bytes (default: 256)",
    )
    parser.add_argument(
        "--warn-budget",
        type=int,
        default=192,
        help="Warning stack threshold in bytes (default: 192)",
    )

    args = parser.parse_args()

    target_files = collect_target_files(args)
    if not target_files:
        location = args.input if args.input else args.input_dir
        print(f"{COLOR_YELLOW}[WARN] No stack usage files found in: {location}{COLOR_RESET}")
        return 0

    entries: List[Tuple[str, int, str]] = []
    for file_path in target_files:
        if args.compiler == "gcc":
            entries.extend(parse_gcc_su_file(file_path))
        else:
            entries.extend(parse_clang_readelf(file_path, args.readelf))

    if not entries:
        print(f"{COLOR_YELLOW}[WARN] No valid stack usage records found in collected files.{COLOR_RESET}")
        return 0

    # Sort descending by stack size
    entries.sort(key=lambda x: x[1], reverse=True)

    print(
        f"\n{COLOR_CYAN}=== Static Stack Usage Report ({args.compiler.upper()}) ==={COLOR_RESET}"
    )
    print(f"Scanned files: {len(target_files)}")
    print(f"{'Stack Size':>12} | {'Budget':>8} | {'Status':>8} | Function Name")
    print("-" * 80)

    failed = False
    for raw_name, size, _ in entries:
        is_ref = is_reference_symbol(raw_name)
        display_name = demangle_symbol(raw_name)

        if is_ref:
            status = f"{COLOR_BLUE}REF{COLOR_RESET}"
        elif size > args.budget:
            status = f"{COLOR_RED}FAIL{COLOR_RESET}"
            failed = True
        elif size > args.warn_budget:
            status = f"{COLOR_YELLOW}WARN{COLOR_RESET}"
        else:
            status = f"{COLOR_GREEN}PASS{COLOR_RESET}"

        print(f"{size:>10} B | {args.budget:>6} B | {status:>17} | {display_name}")

    print("-" * 80)

    if failed:
        print(
            f"{COLOR_RED}[ERROR] One or more microfmt functions exceeded the {args.budget}B stack budget!{COLOR_RESET}\n"
        )
        return 1

    print(
        f"{COLOR_GREEN}[SUCCESS] All microfmt functions are within the {args.budget}B stack budget.{COLOR_RESET}\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())