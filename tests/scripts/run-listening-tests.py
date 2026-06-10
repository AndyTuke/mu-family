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
    # JUCE writes the standalone with a unicode glyph in the filename, and the
    # artefact shape is per-platform. Monorepo layout: build/<plugin>/<plugin>_artefacts/<Config>/Standalone/.
    #   Windows: <Name>.exe   macOS: <Name>.app/Contents/MacOS/<bin>   Linux: bare <bin>
    # This makes the listening pipeline runnable on the macOS CI runner, not just Windows.
    artefacts = REPO_ROOT / 'build' / 'mu-clid' / 'mu-clid_artefacts' / config / 'Standalone'

    exes = sorted(artefacts.glob('*.exe'))
    if exes:
        return exes[0]

    apps = sorted(artefacts.glob('*.app'))
    if apps:
        macos_bins = [p for p in (apps[0] / 'Contents' / 'MacOS').glob('*') if p.is_file()]
        if macos_bins:
            return macos_bins[0]

    # Linux: a bare executable file directly in the Standalone dir.
    linux_bins = [p for p in artefacts.glob('*')
                  if p.is_file() and os.access(p, os.X_OK)]
    if linux_bins:
        return linux_bins[0]

    raise FileNotFoundError(f'no standalone binary in {artefacts} -- run `cmake --build build --config {config}` first')


def resolve_preset(rel: str) -> Path | None:
    """Resolve a preset path from a spec. Content-folder presets (e.g.
    'Rhythms/T11.muRhythm') resolve under CONTENT_ROOT; repo-local test presets
    (e.g. 'tests/presets/TS1.muClid') resolve under REPO_ROOT. Try both."""
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

    # Optional per-rhythm hot-swap (A9): stage a .muRhythm onto one slot mid-render.
    swap_rhy_rel = render.get('swap_rhythm_preset')
    swap_rhy_at  = render.get('swap_rhythm_at')
    if swap_rhy_rel is not None and swap_rhy_at is not None:
        swap_rhy = resolve_preset(swap_rhy_rel)
        if swap_rhy is None:
            print(f'[{test_name}] ERROR: swap_rhythm_preset not found: {swap_rhy_rel}')
            return False
        cmd += ['--swap-rhythm-preset', str(swap_rhy),
                '--swap-rhythm-slot', str(render.get('swap_rhythm_slot', 0)),
                '--swap-rhythm-at', str(swap_rhy_at)]

    # Optional MIDI program-change → full-preset load (A2): seed the ch-9 map and
    # inject a program change mid-render.
    midi_prog        = render.get('midi_program')
    midi_prog_rel    = render.get('midi_program_preset')
    midi_prog_at     = render.get('midi_program_at')
    if midi_prog is not None and midi_prog_rel is not None and midi_prog_at is not None:
        midi_prog_preset = resolve_preset(midi_prog_rel)
        if midi_prog_preset is None:
            print(f'[{test_name}] ERROR: midi_program_preset not found: {midi_prog_rel}')
            return False
        cmd += ['--midi-program', str(midi_prog),
                '--midi-program-preset', str(midi_prog_preset),
                '--midi-program-at', str(midi_prog_at)]

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
