#!/usr/bin/env python3
# Guard: mu-core must not depend on any plugin (mu-clid / future mu-tant / mu-toni).
#
# mu-core is the shared INTERFACE library every plugin links; the dependency has to
# be strictly one-way (plugin -> mu-core, never the reverse). A core file that pulls
# in a plugin header or names a plugin symbol breaks reuse — it's the "looks generic
# but is secretly plugin-coupled" trap (the kind of thing that made #669's RenderMode
# mislabel plausible). A core->plugin dependency *requires* an #include, so the
# include check is the load-bearing one; the namespace check is belt-and-braces.
#
# Comments are stripped before matching, so a doc reference to "PluginProcessor" or
# "mu-clid" in a comment is fine — only real code dependencies fail.
#
# Usage:  python tests/scripts/check-core-boundary.py
# Exits 0 if clean, 1 on any violation, 2 on setup error.

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
CORE = REPO / 'mu-core'

INCLUDE_PLUGIN = re.compile(r'#\s*include\s*[<"][^">]*\bmu-(clid|tant|toni)\b')
PLUGIN_NS      = re.compile(r'\bmu_(clid|tant|toni)::')


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)   # block comments
    text = re.sub(r'//.*', '', text)                    # line comments
    return text


def main() -> int:
    if not CORE.is_dir():
        print(f'check-core-boundary: mu-core not found at {CORE}', file=sys.stderr)
        return 2

    files = sorted(list(CORE.rglob('*.h')) + list(CORE.rglob('*.cpp')))
    fails = 0
    for f in files:
        cleaned = strip_comments(f.read_text(encoding='utf-8', errors='replace'))
        for i, line in enumerate(cleaned.splitlines(), 1):
            if INCLUDE_PLUGIN.search(line):
                print(f'  [FAIL] {f.relative_to(REPO)}:{i} — mu-core includes a plugin header: {line.strip()[:90]}')
                fails += 1
            if PLUGIN_NS.search(line):
                print(f'  [FAIL] {f.relative_to(REPO)}:{i} — mu-core references a plugin namespace: {line.strip()[:90]}')
                fails += 1

    print(f'check-core-boundary: {"PASS" if fails == 0 else "FAIL"} '
          f'({len(files)} mu-core files, {fails} violation(s))')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
