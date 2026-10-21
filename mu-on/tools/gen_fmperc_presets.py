#!/usr/bin/env python3
# mu-On FM Perc factory-preset generator: writes one .muTrack per preset into mu-on/factory/Tracks/.
#
# A .muTrack is the track node (<MuOnTrack>): the engine row, the FM Perc rows (`fmp_<x>`, plain values: Tune in
# Hz, Ratio as a ratio, the rest 0..1), a step row and an empty modulator block, so loading one resets the track's
# modulators. The names follow docs/mu-on/design-ui.md section 2. Values start from the research tables
# (docs/mu-on/research-fm-drums.md 0.1 and 0.2) read through each algorithm's constant table in FmPercEngine.h;
# they are a first pass to be tuned by ear.
#
# Run from the repo root:  python mu-on/tools/gen_fmperc_presets.py
# Copy the result into the factory pack (TDP/Presets/muOn/Tracks) for release.

import os

ALGOS = ["Tom", "Conga", "Zap", "Click / Rim", "Clank", "Wood / Mallet", "Pluck / Ping", "Burst / Glitch"]

# name: (algo, tune Hz, ratio, index, fm env, fdbk, dive, dive time, decay, fold, noise)
PRESETS = {
    # Tom
    "Low Tom":        (0,   90, 1.50, 0.35, 0.57, 0.05, 0.43, 0.46, 0.40, 0.15, 0.15),
    "Mid Tom":        (0,  135, 1.50, 0.35, 0.50, 0.05, 0.36, 0.40, 0.30, 0.15, 0.15),
    "High Tom":       (0,  185, 1.59, 0.30, 0.45, 0.05, 0.30, 0.35, 0.19, 0.10, 0.12),
    "Floor Tom":      (0,   65, 1.41, 0.40, 0.60, 0.10, 0.45, 0.50, 0.50, 0.20, 0.20),
    "Electro Tom":    (0,  120, 1.00, 0.20, 0.35, 0.00, 1.00, 0.55, 0.45, 0.25, 0.50),
    "Rack Tom Tight": (0,  160, 2.14, 0.30, 0.40, 0.05, 0.25, 0.30, 0.15, 0.20, 0.10),
    # Conga
    "Low Conga":      (1,  220, 1.59, 0.35, 0.46, 0.00, 0.40, 0.46, 0.55, 0.10, 0.05),
    "High Conga":     (1,  400, 1.59, 0.35, 0.46, 0.00, 0.40, 0.40, 0.30, 0.10, 0.05),
    "Bongo":          (1,  500, 2.14, 0.30, 0.40, 0.00, 0.35, 0.40, 0.20, 0.10, 0.05),
    "Timbale":        (1,  700, 2.30, 0.50, 0.40, 0.10, 0.25, 0.35, 0.50, 0.20, 0.10),
    # Zap
    "Laser Zap":      (2,  800, 2.70, 0.50, 0.55, 0.10, 0.75, 0.40, 0.35, 0.30, 0.10),
    "Screamer":       (2,  600, 2.10, 0.70, 0.85, 0.25, 0.55, 0.60, 0.65, 0.45, 0.20),
    "Blip":           (2, 1200, 1.50, 0.35, 0.20, 0.00, 0.40, 0.20, 0.10, 0.10, 0.05),
    "Pew Low":        (2,  180, 1.80, 0.45, 0.50, 0.10, 0.80, 0.50, 0.40, 0.25, 0.15),
    # Click / Rim
    "Rim Tick":       (3, 1800, 1.41, 0.50, 0.40, 0.20, 0.20, 0.30, 0.35, 0.20, 0.35),
    "Clave":          (3, 2500, 1.40, 0.30, 0.50, 0.00, 0.15, 0.40, 0.55, 0.05, 0.05),
    "Wood Block":     (3, 1100, 2.70, 0.45, 0.50, 0.10, 0.25, 0.40, 0.50, 0.10, 0.15),
    "Grit Tick":      (3, 2200, 1.90, 0.60, 0.35, 0.60, 0.20, 0.30, 0.30, 0.50, 0.50),
    # Clank
    "Closed Clank":   (4, 1400, 1.00, 0.35, 0.30, 0.10, 0.10, 0.30, 0.12, 0.30, 0.40),
    "Open Clank":     (4, 1200, 1.00, 0.35, 0.50, 0.10, 0.10, 0.30, 0.55, 0.30, 0.40),
    "Gong Hit":       (4,  900, 2.50, 0.60, 0.80, 0.10, 0.20, 0.40, 0.85, 0.10, 0.15),
    "Industrial":     (4,  350, 3.50, 0.80, 0.55, 0.50, 0.30, 0.50, 0.50, 0.70, 0.50),
    # Wood / Mallet
    "Marimba Tap":    (5,  440, 3.98, 0.30, 0.40, 0.00, 0.10, 0.30, 0.50, 0.05, 0.05),
    "Mallet Low":     (5,  200, 3.98, 0.40, 0.45, 0.05, 0.15, 0.40, 0.55, 0.10, 0.10),
    "Mallet High":    (5,  880, 3.98, 0.25, 0.35, 0.00, 0.10, 0.30, 0.40, 0.05, 0.05),
    # Pluck / Ping
    "Ping":           (6,  660, 2.00, 0.45, 0.35, 0.20, 0.43, 0.40, 0.55, 0.05, 0.05),
    "Rising Ping":    (6,  520, 1.00, 0.40, 0.35, 0.15, 0.10, 0.35, 0.50, 0.00, 0.00),
    "Acid Stab":      (6,  110, 1.00, 0.70, 0.45, 0.60, 0.43, 0.30, 0.30, 0.50, 0.05),
    # Burst / Glitch
    "Glitch Burst":   (7, 1000, 1.73, 0.55, 0.40, 0.70, 0.30, 0.30, 0.35, 0.50, 0.40),
    "Digital Crunch": (7,  400, 2.30, 0.80, 0.30, 0.90, 0.15, 0.40, 0.30, 0.70, 0.30),
    "Noise Hit":      (7,  300, 3.10, 0.60, 0.50, 0.50, 0.50, 0.50, 0.40, 0.40, 1.00),
}

KNOBS = ["tune", "ratio", "index", "idec", "fdbk", "dive", "dtime", "decay", "fold", "noise"]
ENGINE_FM_PERC = 5

# The default FM Perc groove (steps 5, 11, 15), the first accented, so a loaded preset plays something.
ON, ACCENT = "0000100000100010", "0000100000000000"


def row(rid, x, choice=None):
    c = ' c="%s"' % choice if choice else ""
    return '  <p id="%s" x="%s"%s/>' % (rid, repr(float(x)), c)


def preset_xml(algo, values):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>', "", '<MuOnTrack engine="FM Perc">',
             row("eng", ENGINE_FM_PERC, "FM Perc"), row("fmp_algo", algo, ALGOS[algo])]
    lines += [row("fmp_" + k, v) for k, v in zip(KNOBS, values)]
    lines.append('  <Track i="0" on="%s" accent="%s"/>' % (ON, ACCENT))
    lines.append('  <Modulators depthUnits="range">')
    for i in range(8):
        lines.append('    <Seq id="cs%d" mode="Stepped" polarity="Unipolar" loopNV="Whole" loopMod="None"' % i)
        lines.append('         loopMult="1" stepNV="Sixteenth" stepMod="None" stepMult="1"/>')
    lines += ["  </Modulators>", "</MuOnTrack>", ""]
    return "\n".join(lines)


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "factory", "Tracks")
    os.makedirs(out, exist_ok=True)
    for name, (algo, *values) in PRESETS.items():
        with open(os.path.join(out, name + ".muTrack"), "w", encoding="utf-8", newline="\n") as f:
            f.write(preset_xml(algo, values))
    print("wrote %d presets to %s" % (len(PRESETS), os.path.normpath(out)))


if __name__ == "__main__":
    main()
