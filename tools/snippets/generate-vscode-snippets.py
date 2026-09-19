#!/usr/bin/env python3
"""Generate .vscode/microfmt.code-snippets from templates under tools/snippets/.

Each entry in tools/snippets/manifest.json points at a plain-text template
file (tools/snippets/templates/*.cpp.tmpl) containing a VS Code snippet body
verbatim, including tabstops/placeholders (${1:name}, $0, ...). Keeping the
template bodies as standalone files makes them easy to read, diff, and edit
directly (e.g. with C++ syntax highlighting), instead of hand-maintaining
escaped strings inside the generated JSON.

Usage:
    python3 tools/snippets/generate-vscode-snippets.py
    python3 tools/snippets/generate-vscode-snippets.py --check   # CI: verify up to date

The generated file is derived output; edit the manifest/templates instead of
.vscode/microfmt.code-snippets directly.
"""

import argparse
import json
import sys
from pathlib import Path

SNIPPETS_DIR = Path(__file__).resolve().parent
TEMPLATES_DIR = SNIPPETS_DIR / "templates"
MANIFEST_PATH = SNIPPETS_DIR / "manifest.json"
OUTPUT_PATH = SNIPPETS_DIR.parent.parent / ".vscode" / "microfmt.code-snippets"

GENERATED_HEADER = (
    "AUTO-GENERATED FILE -- do not edit directly.\n"
    "Regenerate with: python3 tools/snippets/generate-vscode-snippets.py\n"
    "Edit tools/snippets/manifest.json and tools/snippets/templates/*.tmpl instead.\n"
)


def build_snippets() -> dict:
    manifest = json.loads(MANIFEST_PATH.read_text())
    snippets = {}
    for entry in manifest:
        template_path = TEMPLATES_DIR / entry["file"]
        body_text = template_path.read_text()
        # A single trailing newline from the template file is just
        # end-of-file punctuation, not part of the snippet body.
        if body_text.endswith("\n"):
            body_text = body_text[:-1]
        snippets[entry["name"]] = {
            "prefix": entry["prefix"],
            "scope": entry["scope"],
            "description": entry["description"],
            "body": body_text.split("\n"),
        }
    return snippets


def render(snippets: dict) -> str:
    # VS Code parses .code-snippets files as JSONC, so a leading `//`
    # comment block is valid here and keeps the generated-file notice
    # visible to anyone who opens the file directly in the editor.
    header = "\n".join(f"// {line}" if line else "//" for line in GENERATED_HEADER.rstrip("\n").split("\n"))
    return header + "\n" + json.dumps(snippets, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in file matches the templates; exit 1 if stale",
    )
    args = parser.parse_args()

    snippets = build_snippets()
    rendered = render(snippets)

    if args.check:
        current = OUTPUT_PATH.read_text() if OUTPUT_PATH.exists() else ""
        if current != rendered:
            print(f"error: {OUTPUT_PATH} is out of date; run "
                  f"tools/snippets/generate-vscode-snippets.py", file=sys.stderr)
            return 1
        print(f"{OUTPUT_PATH} is up to date")
        return 0

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(rendered)
    print(f"wrote {OUTPUT_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
