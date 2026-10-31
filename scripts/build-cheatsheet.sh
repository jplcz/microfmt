#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

# Joins docs/cheatsheet.md and docs/cheatsheet/*.md into one self-contained
# HTML page (build/cheatsheet/index.html) so the cheatsheet can be searched
# and loaded in one piece. Requires pandoc.

set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly source_dir="$(cd -- "${script_dir}/.." && pwd)"
readonly out_dir="${1:-${source_dir}/build/cheatsheet}"
readonly repo_url="https://github.com/jplcz/microfmt/blob/master"

if ! command -v pandoc >/dev/null 2>&1; then
  printf 'error: pandoc is required to build the cheatsheet page\n' >&2
  exit 1
fi

# Order of the parts in the single page.
readonly parts=(core diagnostics structures time-and-inspector)

mkdir -p "${out_dir}"
readonly joined="${out_dir}/cheatsheet.md"

{
  # Intro: drop the per-page link table, it is replaced by the table of contents.
  awk '/^## Pages$/ {skip = 1; next} /^## / {skip = 0} !skip' "${source_dir}/docs/cheatsheet.md"
  for part in "${parts[@]}"; do
    printf '\n'
    cat "${source_dir}/docs/cheatsheet/${part}.md"
  done
} |
  sed -e "s|](formatters.md)|](${repo_url}/docs/formatters.md)|g" \
      -e "s|](usage.md)|](${repo_url}/docs/usage.md)|g" \
      -e 's|](../cheatsheet.md)|](#formatter-cheatsheet)|g' >"${joined}"

pandoc "${joined}" \
  --from gfm \
  --to html5 \
  --standalone \
  --embed-resources \
  --toc \
  --toc-depth=2 \
  --metadata title="microfmt formatter cheatsheet" \
  --css "${source_dir}/docs/cheatsheet/cheatsheet.css" \
  --output "${out_dir}/index.html"

rm -f "${joined}"
printf 'wrote %s\n' "${out_dir}/index.html"
