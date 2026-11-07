"""Offline audio-artifact sweep. Renders every preset it can find with each product's Debug standalone (--render, headless: no window, no
audio device) at several block sizes / sample rates, and checks each file for: non-finite samples, peaks over full scale, DC offset,
click-like discontinuities (a second difference far above the signal's own), and silence. Results go to a CSV; anything flagged is listed.
Usage: python audio-sweep.py <out_dir> [--seconds 6] [--jobs 4]
Read-only on the repo and on the user's preset folders; writes only under <out_dir>."""
import argparse, concurrent.futures as cf, csv, os, subprocess, sys
import numpy as np
import scipy.io.wavfile as wav

ROOT = r"D:\Dev\mu"
EXE = {
    "mu-clid": ROOT + r"\build\mu-clid\mu-clid_artefacts\Debug\Standalone\mu-Clid.exe",
    "mu-tant": ROOT + r"\build\mu-tant\mu-tant_artefacts\Debug\Standalone\mu-Tant.exe",
    "mu-toni": ROOT + r"\build\mu-toni\mu-toni_artefacts\Debug\Standalone\mu-Toni.exe",
    "mu-on":   ROOT + r"\build\mu-on\mu-on_artefacts\Debug\Standalone\mu-On.exe",
}
EXT = {".muClid": "mu-clid", ".muRhythm": "mu-clid", ".muTant": "mu-tant", ".muPattern": "mu-tant",
       ".muToni": "mu-toni", ".muArp": "mu-toni", ".muOn": "mu-on", ".muTrack": "mu-on"}
CONFIGS = [(48000, 512), (48000, 64), (44100, 480), (96000, 1024)]
DOCS = r"D:\OneDrive\Documents\TDP"
SEARCH = [ROOT + r"\tests\corpus", ROOT + r"\tests\presets", ROOT + r"\mu-on\factory", DOCS + r"\muClid", DOCS + r"\muTant", DOCS + r"\muToni", DOCS + r"\muOn"]


def find_presets():
    out = []
    for base in SEARCH:
        for dp, _, files in os.walk(base):
            if "worktrees" in dp or "_out" in dp:
                continue
            for f in files:
                e = os.path.splitext(f)[1]
                if e in EXT:
                    out.append((EXT[e], os.path.join(dp, f)))
    return sorted(set(out))


def analyse(path):
    sr, raw = wav.read(path)
    # Scale by the file's own sample type (never by the value size: a quiet 32-bit file looks like a loud 16-bit one).
    scale = {np.dtype("int16"): 32768.0, np.dtype("int32"): 2147483648.0, np.dtype("uint8"): 128.0}.get(raw.dtype, 1.0)
    x = raw.astype(np.float64) / scale
    if x.ndim == 1:
        x = x[:, None]
    r = {"finite": bool(np.isfinite(x).all())}
    x = np.nan_to_num(x)
    m = np.abs(x).max(axis=1)
    r["peak"] = float(m.max())
    r["rms"] = float(np.sqrt((x ** 2).mean()))
    r["dc"] = float(np.abs(x.mean(axis=0)).max())
    mono = x[:, 0]
    d2 = np.abs(np.diff(mono, 2))
    r["max_d2"] = float(d2.max()) if len(d2) else 0.0
    # a click: a second difference many times the local typical size and large in absolute terms
    win = 2048
    clicks = 0
    for i in range(0, len(d2) - win, win):
        seg = d2[i:i + win]
        med = np.median(seg) + 1e-9
        if seg.max() > 0.25 and seg.max() > 400 * med:
            clicks += 1
    r["click_windows"] = clicks
    r["silent"] = bool(r["peak"] < 1e-5)
    return r


def render(job):
    prod, preset, sr, bs, secs, out_dir = job
    name = os.path.splitext(os.path.basename(preset))[0].replace(" ", "_")
    wavp = os.path.join(out_dir, f"{prod}-{name}-{sr}-{bs}.wav")
    cmd = [EXE[prod], "--render", "--out", wavp, "--preset", preset, "--seconds", str(secs), "--samplerate", str(sr), "--blocksize", str(bs)]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=240)
        if p.returncode != 0 or not os.path.exists(wavp):
            return (prod, preset, sr, bs, {"error": f"render exit {p.returncode}"})
        res = analyse(wavp)
        # keep only flagged files; delete the rest to save disk
        flagged = (not res["finite"]) or res["peak"] > 1.0 or res["click_windows"] > 0 or res["dc"] > 0.05
        if not flagged:
            os.remove(wavp)
        return (prod, preset, sr, bs, res)
    except subprocess.TimeoutExpired:
        return (prod, preset, sr, bs, {"error": "timeout"})


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    presets = find_presets()
    jobs = [(p, f, sr, bs, a.seconds, a.out_dir) for (p, f) in presets for (sr, bs) in CONFIGS]
    print(f"{len(presets)} presets x {len(CONFIGS)} configs = {len(jobs)} renders", flush=True)
    rows, flagged = [], []
    with cf.ThreadPoolExecutor(a.jobs) as ex, open(os.path.join(a.out_dir, "sweep.csv"), "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["product", "preset", "sr", "block", "finite", "peak", "rms", "dc", "max_d2", "click_windows", "silent", "error"])
        for i, (prod, preset, sr, bs, r) in enumerate(ex.map(render, jobs)):
            w.writerow([prod, preset, sr, bs, r.get("finite"), r.get("peak"), r.get("rms"), r.get("dc"), r.get("max_d2"), r.get("click_windows"), r.get("silent"), r.get("error")])
            fh.flush()
            bad = "error" in r or not r.get("finite", True) or r.get("peak", 0) > 1.0 or r.get("click_windows", 0) > 0 or r.get("dc", 0) > 0.05
            if bad:
                flagged.append((prod, preset, sr, bs, r))
                print("FLAG", prod, os.path.basename(preset), sr, bs, r, flush=True)
            if i % 25 == 0:
                print(f"{i}/{len(jobs)} done, {len(flagged)} flagged", flush=True)
    print(f"DONE {len(jobs)} renders, {len(flagged)} flagged", flush=True)
