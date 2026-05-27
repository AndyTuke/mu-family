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


# --- Modulation metrics (A3-A7) ----------------------------------------------
#
# These measure *change over time* within a window: modulation is "working" when
# the quantity it drives (brightness / level / pitch / hit density) moves across
# the render. Each splits the window into sub-windows and reports a range/count.

def _subwindows(audio: Audio, ch: np.ndarray, t0: float, t1: float, win_ms: float) -> list:
    win = audio.window(ch, t0, t1)
    n = max(1, int(win_ms * audio.rate / 1000))
    n_chunks = max(1, len(win) // n)
    return [win[i * n:(i + 1) * n] for i in range(n_chunks)]


def _centroid(seg: np.ndarray, rate: int, f_lo: float, f_hi: float) -> float:
    if len(seg) < 256:
        return 0.0
    freqs, psd = welch(seg.astype(np.float64), fs=rate, nperseg=min(len(seg), 4096))
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    if not mask.any():
        return 0.0
    f, pw = freqs[mask], psd[mask]
    s = pw.sum()
    return float((f * pw).sum() / s) if s > 0 else 0.0


def _peak(seg: np.ndarray, rate: int, f_lo: float, f_hi: float) -> float:
    if len(seg) < 256:
        return 0.0
    freqs, psd = welch(seg.astype(np.float64), fs=rate, nperseg=min(len(seg), 4096))
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    if not mask.any():
        return 0.0
    return float(freqs[mask][int(np.argmax(psd[mask]))])


def assert_spectral_centroid_range(audio: Audio, p: dict) -> tuple[bool, float]:
    # max-min of the per-sub-window spectral centroid (Hz). High = the spectral
    # brightness is sweeping → a filter-cutoff-style modulation is active.
    ch = audio.channel(p.get('channel', 'any'))
    t0, t1 = p.get('t_window', [0.0, 1e9])
    f_lo, f_hi = p.get('search_hz', [20.0, 12000.0])
    cents = [_centroid(s, audio.rate, f_lo, f_hi)
             for s in _subwindows(audio, ch, t0, t1, p.get('window_ms', 120))]
    cents = [c for c in cents if c > 0]
    actual = (max(cents) - min(cents)) if len(cents) >= 2 else 0.0
    return (p.get('min_hz', 0.0) <= actual <= p.get('max_hz', float('inf'))), actual


def assert_rms_range_db(audio: Audio, p: dict) -> tuple[bool, float]:
    # max-min of per-sub-window RMS (dB), ignoring sub-windows below `floor_dbfs`
    # (the silent gaps between hits). High = level/envelope is being modulated.
    ch = audio.channel(p.get('channel', 'any'))
    t0, t1 = p.get('t_window', [0.0, 1e9])
    levels = [dbfs(rms(s)) for s in _subwindows(audio, ch, t0, t1, p.get('window_ms', 60))]
    levels = [l for l in levels if l > p.get('floor_dbfs', -90.0)]
    actual = (max(levels) - min(levels)) if len(levels) >= 2 else 0.0
    return (p.get('min_db', 0.0) <= actual <= p.get('max_db', float('inf'))), actual


def assert_spectral_peak_range(audio: Audio, p: dict) -> tuple[bool, float]:
    # max-min of the per-sub-window strongest spectral peak (Hz). High = the
    # fundamental is moving → pitch modulation (vibrato) is active.
    ch = audio.channel(p.get('channel', 'any'))
    t0, t1 = p.get('t_window', [0.0, 1e9])
    f_lo, f_hi = p.get('search_hz', [40.0, 4000.0])
    peaks = [_peak(s, audio.rate, f_lo, f_hi)
             for s in _subwindows(audio, ch, t0, t1, p.get('window_ms', 150))]
    peaks = [pk for pk in peaks if pk > 0]
    actual = (max(peaks) - min(peaks)) if len(peaks) >= 2 else 0.0
    return (p.get('min_hz', 0.0) <= actual <= p.get('max_hz', float('inf'))), actual


def assert_onset_count(audio: Audio, p: dict) -> tuple[bool, float]:
    # Count rising-edge onsets: sub-window RMS crossing `threshold_dbfs` from
    # below, with a refractory gap so one hit isn't counted twice. Lets a test
    # assert how many hits land in a window (e.g. pattern modulation changing the
    # hit density between the first and second half of the render).
    ch = audio.channel(p.get('channel', 'any'))
    t0, t1 = p.get('t_window', [0.0, 1e9])
    win_ms = p.get('window_ms', 10)
    thr = 10.0 ** (p.get('threshold_dbfs', -35.0) / 20.0)
    refractory = max(1, int(p.get('refractory_ms', 40) / win_ms))
    count, cooldown, prev_above = 0, 0, False
    for seg in _subwindows(audio, ch, t0, t1, win_ms):
        above = rms(seg) >= thr
        if cooldown > 0:
            cooldown -= 1
        if above and not prev_above and cooldown == 0:
            count += 1
            cooldown = refractory
        prev_above = above
    actual = float(count)
    return (p.get('min', 0) <= actual <= p.get('max', float('inf'))), actual


HANDLERS = {
    'peak_dbfs': assert_peak_dbfs,
    'rms_dbfs': assert_rms_dbfs,
    'duration_above_threshold': assert_duration_above_threshold,
    'spectral_peak_near': assert_spectral_peak_near,
    'spectral_centroid_range': assert_spectral_centroid_range,
    'rms_range_db': assert_rms_range_db,
    'spectral_peak_range': assert_spectral_peak_range,
    'onset_count': assert_onset_count,
}


# --- Main ---------------------------------------------------------------------

def fmt(metric: str, actual: float) -> str:
    if metric in ('peak_dbfs', 'rms_dbfs'):
        return f'{actual:.2f} dBFS'
    if metric == 'duration_above_threshold':
        return f'{actual:.3f} s'
    if metric == 'spectral_peak_near':
        return f'{actual:.1f} Hz'
    if metric in ('spectral_centroid_range', 'spectral_peak_range'):
        return f'{actual:.1f} Hz range'
    if metric == 'rms_range_db':
        return f'{actual:.2f} dB range'
    if metric == 'onset_count':
        return f'{actual:.0f} onsets'
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
