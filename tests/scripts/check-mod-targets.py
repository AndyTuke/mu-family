#!/usr/bin/env python3
# Guard: every product follows the family modulation-target standard
# (mu-core Modulation/ModTarget.h, docs/design-plugin-family.md "Modulation targets").
#
# The rule: depth is a percentage of the target knob's range, and each product defines its
# targets in ONE table of mu_mod::ModTarget rows. This fails if a product
#   - brings back per-target depth scales (registerDepthScale / depthScale... anywhere), or
#   - defines its own target-row struct instead of using mu_mod::ModTarget, or
#   - has no target table built from mu_mod::ModTarget at all.
#
# Comments are stripped before matching, so prose that mentions the old names is fine.
#
# Usage:  python tests/scripts/check-mod-targets.py
# Exits 0 if clean, 1 on any violation, 2 on setup error.

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
PRODUCTS = ['mu-clid', 'mu-tant', 'mu-toni', 'mu-on']

SCALE_API   = re.compile(r'\b(registerDepthScale|depthScaleFor|depthScaleRegistry)\b')
OWN_ROW     = re.compile(r'\bstruct\s+(ModDest|ModDestEntry|Dest|ModTargetRow)\s*\{')
USES_TARGET = re.compile(r'\bmu_mod::ModTarget\b')


def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    text = re.sub(r'//.*', '', text)
    return text


def sources(root: Path):
    return sorted(list(root.rglob('*.h')) + list(root.rglob('*.cpp')))


def main() -> int:
    fails = 0

    # mu-core keeps no scale registry either.
    core = REPO / 'mu-core'
    if not core.is_dir():
        print(f'check-mod-targets: mu-core not found at {core}', file=sys.stderr)
        return 2
    for f in sources(core):
        code = strip_comments(f.read_text(encoding='utf-8', errors='replace'))
        if SCALE_API.search(code):
            print(f'  [FAIL] {f.relative_to(REPO)} - per-target depth scales are gone; depth is % of the knob range')
            fails += 1

    for product in PRODUCTS:
        root = REPO / product / 'Source'
        if not root.is_dir():
            print(f'check-mod-targets: {root} not found', file=sys.stderr)
            return 2
        has_table = False
        for f in sources(root):
            code = strip_comments(f.read_text(encoding='utf-8', errors='replace'))
            rel = f.relative_to(REPO)
            if SCALE_API.search(code):
                print(f'  [FAIL] {rel} - per-target depth scale; add a table row instead (depth = % of range)')
                fails += 1
            if OWN_ROW.search(code):
                print(f'  [FAIL] {rel} - own modulation-target struct; use mu_mod::ModTarget')
                fails += 1
            if 'Modulation' in f.parts and USES_TARGET.search(code):
                has_table = True
        if not has_table:
            print(f'  [FAIL] {product} - no modulation target table of mu_mod::ModTarget rows found')
            fails += 1

    status = 'PASS' if fails == 0 else 'FAIL'
    print(f'check-mod-targets: {status} ({len(PRODUCTS)} products, {fails} violation(s))')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
