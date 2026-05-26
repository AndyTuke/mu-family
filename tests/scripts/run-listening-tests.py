#!/usr/bin/env python3
# Orchestrator: walk tests/expectations/*.json, run the standalone in --render
# mode against each test's preset, then run analyse.py against the rendered WAV.
# Prints a per-test pass/fail summary and exits non-zero if any test fails.
#
# Usage (from repo root):
#     python tests/scripts/run-listening-tests.py
#     python tests/scripts/run-listening-tests.py --config Debug
#     python tests/scripts/run-listening-tests.py --filter T12

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

# Standalone .exe paths contain the unicode mu glyph; force stdout to UTF-8 so
# `print(exe_path)` doesn't crash on Windows cp1252.
sys.stdout.reconfigure(encoding='utf-8')
sys.stderr.reconfigure(encoding='utf-8')

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
EXPECTATIONS_DIR = REPO_ROOT / 'tests' / 'expectations'
OUTPUT_DIR = REPO_ROOT / 'tests' / '_out'
CONTENT_ROOT = Path(os.environ.get('MUCLID_CONTENT_DIR',
                                   r'D:\OneDrive\Documents\TDP\muClid'))


def standalone_exe(config: str) -> Path:
    # JUCE writes the standalone exe with a unicode glyph in the filename.
    # The standard location is build/mu-clid_artefacts/<config>/Standalone/.
    # Monorepo layout: artefacts live under build/<plugin>/<plugin>_artefacts/<Config>/.
    artefacts = REPO_ROOT / 'build' / 'mu-clid' / 'mu-clid_artefacts' / config / 'Standalone'
    # Pick whatever .exe lives there -- the JUCE filename is environment-dependent.
    candidates = sorted(artefacts.glob('*.exe'))
    if not candidates:
        raise FileNotFoundError(f'no standalone .exe in {artefacts} -- run `cmake --build build --config {config}` first')
    return candidates[0]


def resolve_preset(rel: str) -> Path | None:
    """Resolve a preset path from a spec. Content-folder presets (e.g.
    'Rhythms/T11.muRhyth') resolve under CONTENT_ROOT; repo-local test presets
    (e.g. 'tests/presets/TS1.muclid') resolve under REPO_ROOT. Try both."""
    for base in (CONTENT_ROOT, REPO_ROOT):
        p = base / rel
        if p.exists():
            return p
    return None


def run_one(test_name: str, spec_path: Path, exe: Path, verbose: bool) -> bool:
    spec = json.loads(spec_path.read_text(encoding='utf-8'))
    render = spec.get('render', {})

    preset_rel = render.get('preset')
    if preset_rel is None:
        print(f'[{test_name}] SKIP (no render.preset specified)')
        return False
    preset = resolve_preset(preset_rel)
    if preset is None:
        print(f'[{test_name}] ERROR: preset not found: {preset_rel} (looked under content + repo roots)')
        return False

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    wav = OUTPUT_DIR / f'{test_name}.wav'

    cmd = [
        str(exe),
        '--render', '--out', str(wav),
        '--preset', str(preset),
        '--seconds', str(render.get('seconds', 2.0)),
        '--samplerate', str(render.get('sample_rate', 48000)),
        '--blocksize', str(render.get('block_size', 512)),
    ]

    # Optional mid-render preset swap (exercises the deferred / prestaged /
    # tail-out full-preset hot-swap). Both keys required to activate.
    swap_rel = render.get('swap_preset')
    swap_at  = render.get('swap_at')
    if swap_rel is not None and swap_at is not None:
        swap_preset = resolve_preset(swap_rel)
        if swap_preset is None:
            print(f'[{test_name}] ERROR: swap_preset not found: {swap_rel}')
            return False
        cmd += ['--swap-preset', str(swap_preset), '--swap-at', str(swap_at)]

    if verbose:
        print(f'[{test_name}] $ {" ".join(cmd)}')

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f'[{test_name}] RENDER FAILED (exit {res.returncode})')
        if res.stderr:
            print(res.stderr)
        return False

    # Analyse step.
    analyser = REPO_ROOT / 'tests' / 'scripts' / 'analyse.py'
    res = subprocess.run([sys.executable, str(analyser), str(wav), str(spec_path)],
                         capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    if res.stderr:
        sys.stderr.write(res.stderr)
    return res.returncode == 0


def main(argv) -> int:
    parser = argparse.ArgumentParser(description='Run mu-Clid listening-test suite')
    parser.add_argument('--config', default='Release', choices=['Debug', 'Release'])
    parser.add_argument('--filter', default=None,
                        help='Only run tests whose name contains this substring')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args(argv)

    exe = standalone_exe(args.config)
    print(f'standalone: {exe}')
    print(f'expectations: {EXPECTATIONS_DIR}')
    print(f'content root: {CONTENT_ROOT}')
    print()

    specs = sorted(EXPECTATIONS_DIR.glob('*.json'))
    if args.filter:
        specs = [s for s in specs if args.filter in s.stem]
    if not specs:
        print('no expectations found', file=sys.stderr)
        return 2

    results = []
    for spec in specs:
        name = spec.stem
        print(f'--- {name} ---')
        ok = run_one(name, spec, exe, args.verbose)
        results.append((name, ok))
        print()

    passes = sum(1 for _, ok in results if ok)
    print(f'=== {passes}/{len(results)} tests PASS ===')
    for name, ok in results:
        print(f'  {"[OK]  " if ok else "[FAIL]"} {name}')
    return 0 if passes == len(results) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
