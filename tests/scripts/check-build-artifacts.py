#!/usr/bin/env python3
# C6 - post-build artefact guard.
#
# Two regression guards that don't need audio:
#   1. UTF-8 plugin-name integrity (#654): every built mu-Clid binary must carry
#      the display name with a real UTF-8 mu (CE BC) "u-Clid" and must NOT contain
#      a mangled "?-Clid" - the exact symptom when the /utf-8 MSVC flag is lost and
#      the -D PLUGIN_NAME define round-trips through the system code page.
#   2. Build-number source consistency: build_number.txt must equal BUILD_NUMBER
#      in mu-clid/Source/BuildNumber.h, so the deployed artefacts can't disagree
#      about their version ("versions should always match").
#
# Usage (from repo root):
#     python tests/scripts/check-build-artifacts.py [--config Release]
# Exits 0 if all checks pass, 1 on any failure, 2 on setup error.

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
MU = b'\xce\xbc'                 # UTF-8 micro sign U+00B5 / Greek mu
GOOD = MU + b'-Clid'             # "u-Clid" with a real mu
BAD = b'?-Clid'                  # mangled mu


def find_binaries(config: str) -> list[Path]:
    base = REPO_ROOT / 'build' / 'mu-clid'
    pats = [
        f'mu-clid_artefacts/{config}/Standalone/*.exe',
        f'mu-clid_artefacts/{config}/CLAP/*.clap',
        f'mu-clid_artefacts/{config}/VST3/**/*.vst3',
        f'mu-clid-lite_artefacts/{config}/CLAP/*.clap',
        f'mu-clid-lite_artefacts/{config}/VST3/**/*.vst3',
    ]
    found: list[Path] = []
    for p in pats:
        found += [f for f in base.glob(p) if f.is_file()]
    return sorted(found)


def check_build_number() -> tuple[bool, str]:
    txt = (REPO_ROOT / 'build_number.txt').read_text(encoding='utf-8').strip()
    hdr = (REPO_ROOT / 'mu-clid' / 'Source' / 'BuildNumber.h').read_text(encoding='utf-8')
    m = re.search(r'#define\s+BUILD_NUMBER\s+(\d+)', hdr)
    hdr_num = m.group(1) if m else '(not found)'
    ok = (txt == hdr_num)
    return ok, f'build_number.txt={txt} BuildNumber.h={hdr_num}'


def main(argv) -> int:
    ap = argparse.ArgumentParser(description='Post-build artefact guard (#654 + version)')
    ap.add_argument('--config', default='Release', choices=['Debug', 'Release'])
    args = ap.parse_args(argv)

    fails = 0

    ok, detail = check_build_number()
    print(f'  [{"OK" if ok else "FAIL"}] build-number consistency: {detail}')
    fails += (not ok)

    binaries = find_binaries(args.config)
    if not binaries:
        print(f'check-build-artifacts: no {args.config} artefacts under build/ '
              f'-- run `cmake --build build --config {args.config}` first', file=sys.stderr)
        return 2

    for b in binaries:
        data = b.read_bytes()
        good = GOOD in data
        bad = BAD in data
        ok = good and not bad
        fails += (not ok)
        note = 'µ-Clid OK' if good else 'MISSING µ-Clid'
        if bad:
            note += ' + FOUND mangled ?-Clid (#654 regression!)'
        print(f'  [{"OK" if ok else "FAIL"}] {b.relative_to(REPO_ROOT)} : {note}')

    print(f'check-build-artifacts: {"PASS" if fails == 0 else "FAIL"} '
          f'({len(binaries)} binaries, {fails} failure(s))')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
