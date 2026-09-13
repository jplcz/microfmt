#!/usr/bin/env python3
"""
microfmt stack usage analyzer
Parses GCC `.su` files and Clang `llvm-readelf --stack-sizes` output.
"""

import argparse
import os
import re
import subprocess
import sys
from typing import List, Tuple

# ANSI terminal colors
COLOR_RED = "\033[31;1m"
COLOR_YELLOW = "\033[33;1m"
COLOR_GREEN = "\033[32m"
COLOR_CYAN = "\033[36m"
COLOR_RESET = "\033[0m"


def parse_gcc_su_file(su_path: str) -> List[Tuple[str, int, str]]:
    """
    Parses GCC .su format:
    <file>:<line>:<col>:<func_name>\t<size>\t<type>
    Example: tests/stack_probes.cpp:24:6:probe_stack_0_args\t32\tstatic
    """
    results = []
    if not os.path.exists(su_path):
        return results

    with open(su_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                loc_func = parts[0]
                size_str = parts[1]
                alloc_type = parts[2] if len(parts) > 2 else "static"

                func_name = loc_func.split(":")[-1] if ":" in loc_func else loc_func
                try:
                    size = int(size_str)
                    results.append((func_name, size, alloc_type))
                except ValueError:
                    continue
    return results


def parse_clang_readelf(obj_path: str, readelf_bin: str) -> List[Tuple[str, int, str]]:
    """
    Extracts stack sizes using `llvm-readelf --stack-sizes <obj_path>`
    """
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

    # Regex matches size and symbol name from llvm-readelf output:
    # 32 probe_stack_0_args or 0x00000020 probe_stack_0_args
    pattern = re.compile(r"^\s*([0-9a-fA-Fx]+)\s+(.+)$")
    in_table = False

    for line in proc.stdout.splitlines():
        line = line.strip()
        if "Stack Sizes:" in line or "Size" in line and "Function" in line:
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Static Stack Usage Analyzer for microfmt")
    parser.add_argument("--compiler", choices=["gcc", "clang"], required=True, help="Compiler flavor")
    parser.add_argument("--input", required=True, help="Path to .su file (GCC) or .o object file (Clang)")
    parser.add_argument("--readelf", default="llvm-readelf", help="Path to llvm-readelf / readelf binary (Clang only)")
    parser.add_argument("--budget", type=int, default=256, help="Hard maximum stack frame budget in bytes (default: 256)")
    parser.add_argument("--warn-budget", type=int, default=192, help="Warning stack threshold in bytes (default: 192)")

    args = parser.parse_args()

    if args.compiler == "gcc":
        entries = parse_gcc_su_file(args.input)
    else:
        entries = parse_clang_readelf(args.input, args.readelf)

    if not entries:
        print(f"{COLOR_YELLOW}[WARN] No stack usage records found in: {args.input}{COLOR_RESET}")
        return 0

    # Sort descending by stack size
    entries.sort(key=lambda x: x[1], reverse=True)

    print(f"\n{COLOR_CYAN}=== Static Stack Usage Report ({args.compiler.upper()}) ==={COLOR_RESET}")
    print(f"{'Stack Size':>12} | {'Budget':>8} | {'Status':>8} | Function Name")
    print("-" * 65)

    failed = False
    for func_name, size, _ in entries:
        if size > args.budget:
            status = f"{COLOR_RED}FAIL{COLOR_RESET}"
            failed = True
        elif size > args.warn_budget:
            status = f"{COLOR_YELLOW}WARN{COLOR_RESET}"
        else:
            status = f"{COLOR_GREEN}PASS{COLOR_RESET}"

        print(f"{size:>10} B | {args.budget:>6} B | {status:>17} | {func_name}")

    print("-" * 65)

    if failed:
        print(f"{COLOR_RED}[ERROR] One or more functions exceeded the {args.budget}B stack budget!{COLOR_RESET}\n")
        return 1

    print(f"{COLOR_GREEN}[SUCCESS] All probed functions are within the {args.budget}B stack budget.{COLOR_RESET}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
