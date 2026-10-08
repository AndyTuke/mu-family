#!/usr/bin/env python3
# Orchestrator: walk tests/expectations/*.json, run each test's product standalone in
# --render mode (the shared mu-core render, same flags for every product), then run
# analyse.py against the rendered WAV. Each expectation names its "product" (default
# mu-clid). Prints a per-test summary; exits non-zero if any test fails (skips don't).
#
# Usage (from repo root):
#     python tests/scripts/run-listening-tests.py
#     python tests/scripts/run-listening-tests.py --config Debug
#     python tests/scripts/run-listening-tests.py --filter T12
#     python tests/scripts/run-listening-tests.py --product mu-tant

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

PRODUCTS = ('mu-clid', 'mu-tant', 'mu-toni', 'mu-on')
CONTENT_FOLDER = {'mu-clid': 'muClid', 'mu-tant': 'muTant', 'mu-toni': 'muToni', 'mu-on': 'muOn'}
FULL_PRESET_EXT = {'mu-clid': 'muClid', 'mu-tant': 'muTant', 'mu-toni': 'muToni', 'mu-on': 'muOn'}
TDP_ROOT = Path(os.environ.get('MU_CONTENT_ROOT', r'D:\OneDrive\Documents\TDP'))


def content_root(product: str) -> Path:
    # mu-Clid keeps its historical override; every product defaults to TDP/<folder>.
    if product == 'mu-clid' and 'MUCLID_CONTENT_DIR' in os.environ:
        return Path(os.environ['MUCLID_CONTENT_DIR'])
    return TDP_ROOT / CONTENT_FOLDER[product]


def standalone_exe(config: str, product: str) -> Path:
    # JUCE writes the standalone with a unicode glyph in the filename, and the
    # artefact shape is per-platform. Monorepo layout: build/<plugin>/<plugin>_artefacts/<Config>/Standalone/.
    #   Windows: <Name>.exe   macOS: <Name>.app/Contents/MacOS/<bin>   Linux: bare <bin>
    artefacts = REPO_ROOT / 'build' / product / f'{product}_artefacts' / config / 'Standalone'

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


def resolve_preset(rel: str, product: str) -> Path | None:
    """Resolve a preset path from a spec. Content-folder presets (e.g.
    'Rhythms/T11.muRhythm') resolve under the product's content root; repo-local test
    presets (e.g. 'tests/presets/TS1.muClid') resolve under REPO_ROOT. Try both."""
    for base in (content_root(product), REPO_ROOT):
        p = base / rel
        if p.exists():
            return p
    return None


def wav_max_difference(a: Path, b: Path) -> float:
    """The largest per-sample difference between two renders, in 24-bit LSBs (inf when their
    shapes differ). Integer WAV data is scaled to full scale first, whatever its container width."""
    import numpy as np
    from scipy.io import wavfile
    ra, xa = wavfile.read(a)
    rb, xb = wavfile.read(b)
    if ra != rb or xa.shape != xb.shape:
        return float('inf')
    def full_scale(x):
        return x.astype(np.float64) / (np.iinfo(x.dtype).max + 1.0) if np.issubdtype(x.dtype, np.integer) else x.astype(np.float64)
    return float(np.max(np.abs(full_scale(xa) - full_scale(xb)))) * 2 ** 23 if xa.size else 0.0


def run_one(test_name: str, spec: dict, spec_path: Path, exe: Path, product: str, verbose: bool) -> str:
    """Render + analyse one test. Returns 'pass' or 'fail'.

    With render.roundtrip the start state is also saved as a full preset (--save-preset) and the
    test is rendered a second time from that file: both renders must be sample-identical, which
    proves the save / load round trip (including an older-format start preset) loses nothing."""
    render = spec.get('render', {})

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    wav = OUTPUT_DIR / f'{test_name}.wav'
    roundtrip_preset = OUTPUT_DIR / f'{test_name}_roundtrip.{FULL_PRESET_EXT[product]}'
    cmd = [
        str(exe),
        '--render', '--out', str(wav),
        '--seconds', str(render.get('seconds', 2.0)),
        '--samplerate', str(render.get('sample_rate', 48000)),
        '--blocksize', str(render.get('block_size', 512)),
    ]

    def resolve(key: str) -> Path | None:
        p = resolve_preset(render[key], product)
        if p is None:
            print(f'[{test_name}] ERROR: {key} not found: {render[key]} (looked under content + repo roots)')
        return p

    # Starting preset — optional: with none the product renders its factory state.
    if render.get('preset') is not None:
        preset = resolve('preset')
        if preset is None:
            return 'fail'
        cmd += ['--preset', str(preset)]
        if 'preset_slot' in render:
            cmd += ['--preset-slot', str(render['preset_slot'])]

    if 'play' in render:
        cmd += ['--play' if render['play'] else '--no-play']

    if render.get('roundtrip'):
        roundtrip_preset.unlink(missing_ok=True)
        cmd += ['--save-preset', str(roundtrip_preset)]

    # Optional mid-render full-preset swap (deferred to its boundary). Both keys required.
    if render.get('swap_preset') is not None and render.get('swap_at') is not None:
        swap_preset = resolve('swap_preset')
        if swap_preset is None:
            return 'fail'
        cmd += ['--swap-preset', str(swap_preset), '--swap-at', str(render['swap_at'])]

    # Optional per-slot hot-swap (A9): stage a slot preset onto one slot mid-render.
    # mu-Clid's original swap_rhythm_* keys are accepted as aliases of swap_slot_*.
    slot_key = 'swap_slot_preset' if 'swap_slot_preset' in render else 'swap_rhythm_preset'
    slot_at = render.get('swap_slot_at', render.get('swap_rhythm_at'))
    if render.get(slot_key) is not None and slot_at is not None:
        swap_slot = resolve(slot_key)
        if swap_slot is None:
            return 'fail'
        cmd += ['--swap-slot-preset', str(swap_slot),
                '--swap-slot', str(render.get('swap_slot', render.get('swap_rhythm_slot', 0))),
                '--swap-slot-at', str(slot_at)]

    # Optional MIDI program-change -> full-preset load (A2): seed the ch-9 map and
    # inject a program change mid-render.
    if all(render.get(k) is not None for k in ('midi_program', 'midi_program_preset', 'midi_program_at')):
        midi_prog_preset = resolve('midi_program_preset')
        if midi_prog_preset is None:
            return 'fail'
        cmd += ['--midi-program', str(render['midi_program']),
                '--midi-program-preset', str(midi_prog_preset),
                '--midi-program-at', str(render['midi_program_at'])]

    if verbose:
        print(f'[{test_name}] $ {" ".join(cmd)}')

    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f'[{test_name}] RENDER FAILED (exit {res.returncode})')
        if res.stderr:
            print(res.stderr)
        return 'fail'

    # Analyse step.
    analyser = REPO_ROOT / 'tests' / 'scripts' / 'analyse.py'
    res = subprocess.run([sys.executable, str(analyser), str(wav), str(spec_path)],
                         capture_output=True, text=True)
    sys.stdout.write(res.stdout)
    if res.stderr:
        sys.stderr.write(res.stderr)
    if res.returncode != 0:
        return 'fail'
    if render.get('roundtrip'):
        return run_roundtrip(test_name, cmd, wav, roundtrip_preset, verbose)
    return 'pass'


def run_roundtrip(test_name: str, cmd: list, wav: Path, saved: Path, verbose: bool) -> str:
    """Render again from the saved preset (in place of the start preset); the two must match."""
    if not saved.exists():
        print(f'[{test_name}] ROUNDTRIP FAILED: no preset saved at {saved}')
        return 'fail'
    cmd2 = list(cmd)
    for flag in ('--preset', '--preset-slot', '--save-preset'):
        while flag in cmd2:
            i = cmd2.index(flag)
            del cmd2[i:i + 2]
    wav2 = OUTPUT_DIR / f'{test_name}_roundtrip.wav'
    cmd2[cmd2.index('--out') + 1] = str(wav2)
    cmd2 += ['--preset', str(saved)]
    if verbose:
        print(f'[{test_name}] $ {" ".join(cmd2)}')
    res = subprocess.run(cmd2, capture_output=True, text=True)
    if res.returncode != 0:
        print(f'[{test_name}] ROUNDTRIP RENDER FAILED (exit {res.returncode})')
        if res.stderr:
            print(res.stderr)
        return 'fail'
    diff = wav_max_difference(wav, wav2)
    ok = diff <= 1.0   # identical at 24 bits, allowing one LSB
    verdict = 'PASS' if ok else 'FAIL'
    print(f'  [{verdict}] roundtrip_identical: max sample difference {diff:g} (24-bit LSBs)')
    return 'pass' if ok else 'fail'


def main(argv) -> int:
    parser = argparse.ArgumentParser(description='Run the mu-family listening-test suite')
    parser.add_argument('--config', default='Release', choices=['Debug', 'Release'])
    parser.add_argument('--filter', default=None,
                        help='Only run tests whose name contains this substring')
    parser.add_argument('--product', default=None, choices=PRODUCTS,
                        help='Only run tests for this product (default: every product)')
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args(argv)

    print(f'expectations: {EXPECTATIONS_DIR}')
    print()

    specs = sorted(EXPECTATIONS_DIR.glob('*.json'))
    if args.filter:
        specs = [s for s in specs if args.filter in s.stem]
    if not specs:
        print('no expectations found', file=sys.stderr)
        return 2

    results = []
    exes = {}
    for spec_path in specs:
        name = spec_path.stem
        spec = json.loads(spec_path.read_text(encoding='utf-8'))
        product = spec.get('product', 'mu-clid')
        if args.product and product != args.product:
            continue
        print(f'--- {name} ({product}) ---')
        try:
            if product not in exes:
                exes[product] = standalone_exe(args.config, product)
                print(f'standalone: {exes[product]}')
            outcome = run_one(name, spec, spec_path, exes[product], product, args.verbose)
        except FileNotFoundError as e:
            print(f'[{name}] SKIP ({e})')
            outcome = 'skip'
        results.append((name, product, outcome))
        print()

    passes = sum(1 for *_, o in results if o == 'pass')
    fails = sum(1 for *_, o in results if o == 'fail')
    skips = sum(1 for *_, o in results if o == 'skip')
    print(f'=== {passes} PASS, {fails} FAIL, {skips} SKIP ===')
    label = {'pass': '[OK]  ', 'fail': '[FAIL]', 'skip': '[SKIP]'}
    for name, product, outcome in results:
        print(f'  {label[outcome]} {name} ({product})')
    return 0 if fails == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
