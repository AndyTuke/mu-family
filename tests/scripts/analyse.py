#!/usr/bin/env python3
# Analyse a rendered WAV against a per-test expectations JSON.
#
# Usage:
#     python analyse.py <wav> <expectations.json> [--verbose]
#
# Exits 0 if every assertion passes, 1 if any assertion fails, 2 on error.

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch


# --- Metric primitives --------------------------------------------------------

@dataclass
class Audio:
    samples: np.ndarray   # shape (frames, channels), float32 in [-1, 1]
    rate: int

    def channel(self, name: str) -> np.ndarray:
        # 'left' / 'right' / 'mono' / 'any'. 'any' = take the channel with the
        # higher peak so a test for 'audio present' doesn't fail when the signal
        # lands on one side. 'mono' = average across channels.
        c = self.samples
        if name == 'left':  return c[:, 0]
        if name == 'right': return c[:, min(1, c.shape[1] - 1)]
        if name == 'mono':  return c.mean(axis=1)
        if name == 'any':
            peaks = np.max(np.abs(c), axis=0)
            return c[:, int(np.argmax(peaks))]
        raise ValueError(f'unknown channel name: {name}')

    def window(self, ch: np.ndarray, t0: float, t1: float) -> np.ndarray:
        i0 = max(0, int(t0 * self.rate))
        i1 = min(len(ch), int(t1 * self.rate))
        return ch[i0:i1]


def load_wav(path: Path) -> Audio:
    rate, raw = wavfile.read(path)
    # scipy returns int16/int32/float32 depending on the source; normalise to
    # float32 [-1, 1] so downstream metrics are dimensionless and comparable.
    if raw.dtype == np.int16:
        s = raw.astype(np.float32) / 32768.0
    elif raw.dtype == np.int32:
        s = raw.astype(np.float32) / 2147483648.0
    elif raw.dtype == np.uint8:
        s = (raw.astype(np.float32) - 128.0) / 128.0
    else:
        s = raw.astype(np.float32)
    if s.ndim == 1:
        s = s.reshape(-1, 1)
    return Audio(s, int(rate))


def dbfs(x: float) -> float:
    return 20.0 * math.log10(max(abs(x), 1e-12))


def rms(x: np.ndarray) -> float:
    if len(x) == 0:
        return 0.0
    return float(np.sqrt(np.mean(x.astype(np.float64) ** 2)))


# --- Assertion handlers -------------------------------------------------------
#
# Each handler takes (audio, params) and returns a tuple (passed: bool, actual: float).
# 'actual' is the measured value so the report can show what we observed.

def assert_peak_dbfs(audio: Audio, p: dict) -> tuple[bool, float]:
    ch = audio.channel(p.get('channel', 'any'))
    win = audio.window(ch, *p.get('t_window', [0.0, 1e9]))
    peak = float(np.max(np.abs(win))) if len(win) else 0.0
    actual = dbfs(peak)
    lo = p.get('min', float('-inf'))
    hi = p.get('max', float('inf'))
    return (lo <= actual <= hi), actual


def assert_rms_dbfs(audio: Audio, p: dict) -> tuple[bool, float]:
    ch = audio.channel(p.get('channel', 'any'))
    win = audio.window(ch, *p.get('t_window', [0.0, 1e9]))
    r = rms(win)
    actual = dbfs(r)
    lo = p.get('min', float('-inf'))
    hi = p.get('max', float('inf'))
    return (lo <= actual <= hi), actual


def assert_duration_above_threshold(audio: Audio, p: dict) -> tuple[bool, float]:
    # Time (in seconds) the signal envelope stays above `threshold_dbfs`,
    # computed via per-window RMS at `window_ms` resolution. Useful to assert
    # 'audio audible for at least X seconds' / 'silenced within Y seconds'.
    ch = audio.channel(p.get('channel', 'any'))
    t0, t1 = p.get('t_window', [0.0, 1e9])
    win = audio.window(ch, t0, t1)
    win_samples = max(1, int(p.get('window_ms', 20) * audio.rate / 1000))
    threshold = 10.0 ** (p.get('threshold_dbfs', -60.0) / 20.0)
    n_chunks = max(1, len(win) // win_samples)
    chunks = np.array_split(win[: n_chunks * win_samples], n_chunks)
    above = sum(rms(c) >= threshold for c in chunks)
    actual = above * win_samples / audio.rate
    lo = p.get('min_seconds', 0.0)
    hi = p.get('max_seconds', float('inf'))
    return (lo <= actual <= hi), actual


def assert_spectral_peak_near(audio: Audio, p: dict) -> tuple[bool, float]:
    # FFT-based check: in the given window, find the strongest spectral peak
    # and assert it lies within `tolerance_hz` of `frequency_hz`. Catches
    # 'Karplus pitch wrong' / 'filter resonance not at expected frequency'.
    ch = audio.channel(p.get('channel', 'any'))
    win = audio.window(ch, *p.get('t_window', [0.0, 1e9])).astype(np.float64)
    if len(win) < 256:
        return False, 0.0
    nperseg = min(len(win), 8192)
    freqs, psd = welch(win, fs=audio.rate, nperseg=nperseg)
    # Restrict search to a sensible band (default 20–8000 Hz) so DC / aliasing
    # don't dominate.
    f_lo = p.get('search_hz', [20.0, 8000.0])[0]
    f_hi = p.get('search_hz', [20.0, 8000.0])[1]
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    if not mask.any():
        return False, 0.0
    band_psd = psd[mask]
    band_f = freqs[mask]
    peak_f = float(band_f[int(np.argmax(band_psd))])
    target = float(p['frequency_hz'])
    tol = float(p.get('tolerance_hz', 10.0))
    return abs(peak_f - target) <= tol, peak_f


HANDLERS = {
    'peak_dbfs': assert_peak_dbfs,
    'rms_dbfs': assert_rms_dbfs,
    'duration_above_threshold': assert_duration_above_threshold,
    'spectral_peak_near': assert_spectral_peak_near,
}


# --- Main ---------------------------------------------------------------------

def fmt(metric: str, actual: float) -> str:
    if metric in ('peak_dbfs', 'rms_dbfs'):
        return f'{actual:.2f} dBFS'
    if metric == 'duration_above_threshold':
        return f'{actual:.3f} s'
    if metric == 'spectral_peak_near':
        return f'{actual:.1f} Hz'
    return f'{actual:.3f}'


def main(argv) -> int:
    parser = argparse.ArgumentParser(description='Analyse a rendered WAV against expectations JSON')
    parser.add_argument('wav', type=Path)
    parser.add_argument('expectations', type=Path)
    parser.add_argument('--verbose', action='store_true')
    args = parser.parse_args(argv)

    if not args.wav.exists():
        print(f'analyse: wav not found: {args.wav}', file=sys.stderr)
        return 2
    if not args.expectations.exists():
        print(f'analyse: expectations not found: {args.expectations}', file=sys.stderr)
        return 2

    audio = load_wav(args.wav)
    with args.expectations.open('r', encoding='utf-8') as f:
        spec = json.load(f)

    print(f'analyse: {spec.get("test", args.expectations.stem)} '
          f'({audio.samples.shape[0]/audio.rate:.3f}s, {audio.samples.shape[1]}ch @ {audio.rate} Hz)')

    fails = 0
    for a in spec.get('assertions', []):
        name = a['name']
        metric = a['metric']
        handler = HANDLERS.get(metric)
        if handler is None:
            print(f'  [FAIL] {name}: unknown metric {metric!r}')
            fails += 1
            continue
        try:
            ok, actual = handler(audio, a)
        except Exception as e:
            print(f'  [FAIL] {name}: handler error: {e}')
            fails += 1
            continue
        mark = '[OK]' if ok else '[FAIL]'
        line = f'  {mark} {name}: {metric}={fmt(metric, actual)}'
        if a.get('comment') and (args.verbose or not ok):
            line += f'   ({a["comment"]})'
        print(line)
        if not ok:
            fails += 1

    if fails == 0:
        print(f'analyse: PASS ({len(spec.get("assertions", []))} assertions)')
        return 0
    print(f'analyse: FAIL ({fails}/{len(spec.get("assertions", []))} failed)')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
