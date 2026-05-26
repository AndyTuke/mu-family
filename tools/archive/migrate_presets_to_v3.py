#!/usr/bin/env python3
"""
Stage 36 preset library migration: collapse the 9 named insert fields
(drvDrv / drvOut / drvDit / drvTon / drvBits / drvRate / eqLowGain /
eqMidGain / eqHighGain) into 4 generic normalised Param slots
(insP1 / insP2 / insP3 / insP4). Same translation as
PresetIO::migrateInsertSlotsV3 in C++; this script runs it offline so the
shipped library files carry the new format directly.

Usage:
    python migrate_presets_to_v3.py <preset_dir> [--dry-run]

Targets every .muRhyth and .muclid under <preset_dir> (recursive). On each:
  • Per rhythm (the file itself for .muRhyth, each <Rhythm> child for
    .muclid): translate insert fields based on drvChar.
  • Translate any <Asgn> entries in the per-rhythm <Modulators> child whose
    `dest` points at the old insert.* / ks.* / voc.* IDs.
  • For .muclid only: also translate the master-insert fields (mst_ins* and
    mst_ins2*) in the <GlobalState> child.

Files are rewritten in place. A `.v2_backup` copy is kept beside each one
the first time it's migrated.
"""
import argparse
import math
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Algorithm index → algorithm name (must match kInsertAlgorithmNames).
ALGO_NAMES = [
    "None", "SoftClip", "HardClip", "Fold", "Bitcrusher", "Clipper", "EQ",
    "Compressor", "Limiter", "RingMod", "TapeSat", "Karplus", "Vocoder",
    "VocoderSt",
]
ALGO_BY_NAME = {n: i for i, n in enumerate(ALGO_NAMES)}

# Per-algo per-slot (min, max, skew) — must match mu_ui::kInsertAlgoSlots.
# skew: "L" = linear, "G" = log (geometric), "I" = int-step (linear with round).
# A None entry means the slot is unused for that algorithm.
SLOTS = {
    0:  [None, None, None, None],
    1:  [( 0.0, 100.0, "L"), (-24.0,  0.0,  "L"), None,                  (20.0, 20000.0, "G")],
    2:  [( 0.0, 100.0, "L"), (-24.0,  0.0,  "L"), None,                  (20.0, 20000.0, "G")],
    3:  [( 0.0, 100.0, "L"), (-24.0,  0.0,  "L"), None,                  (20.0, 20000.0, "G")],
    4:  [( 1.0, 16.0,  "I"), (100.0, 48000.0,"G"), (0.0, 100.0, "L"),    (20.0, 20000.0, "G")],
    5:  [( 0.0, 100.0, "L"), (-24.0,  0.0,  "L"), None,                  (20.0, 20000.0, "G")],
    6:  [(-18.0, 18.0, "L"), (-18.0, 18.0,  "L"), (200.0, 8000.0, "G"),  (-18.0, 18.0,   "L")],
    7:  [( 0.0, 100.0, "L"), (-24.0, 24.0,  "L"), (0.0, 100.0, "L"),     (20.0, 2000.0,  "G")],
    8:  [( 0.0, 100.0, "L"), (-24.0, 24.0,  "L"), (0.0, 100.0, "L"),     (20.0, 2000.0,  "G")],
    9:  [( 0.0, 100.0, "L"), None,                None,                  (10.0, 5000.0,  "G")],
    10: [( 0.0, 100.0, "L"), (-24.0,  0.0,  "L"), None,                  (200.0, 20000.0,"G")],
    11: [( 0.0, 11.0,  "I"), (0.0, 3.0,    "I"), (0.0, 100.0, "L"),     (20.0, 20000.0, "G")],
    12: [( 0.0,  3.0,  "I"), (0.0, 6.0,    "I"), (1.0, 5.0,   "I"),     (1.0, 12.0,     "I")],
    13: [( 0.0,  3.0,  "I"), (0.0, 6.0,    "I"), (1.0, 5.0,   "I"),     (1.0, 12.0,     "I")],
}


def actual_to_norm(actual: float, algo: int, slot: int) -> float:
    """Convert an actual slot value to its 0..1 normalised storage form."""
    cfg = SLOTS[algo][slot]
    if cfg is None:
        return 0.0
    mn, mx, sk = cfg
    actual = max(mn, min(mx, actual))
    if sk == "G":
        lo = max(0.0001, mn)
        if mx <= lo:
            return 0.0
        return math.log(actual / lo) / math.log(mx / lo)
    span = mx - mn
    return (actual - mn) / span if span > 0 else 0.0


def field_float(el: ET.Element, name: str, default: float) -> float:
    """Read an XML attribute as float, with default fallback."""
    v = el.get(name)
    if v is None:
        return default
    try:
        return float(v)
    except ValueError:
        return default


def migrate_insert(el: ET.Element, prefix: str) -> bool:
    """Apply per-rhythm v3 insert migration. Returns True if anything changed."""
    char_attr = prefix + "drvChar"
    char_name = el.get(char_attr)
    if char_name is None or char_name not in ALGO_BY_NAME:
        return False
    if el.get(prefix + "insP1") is not None:
        return False  # already v3

    algo = ALGO_BY_NAME[char_name]
    oldDrv = field_float(el, prefix + "drvDrv", 0.0)
    oldOut = field_float(el, prefix + "drvOut", 0.0)
    oldDit = field_float(el, prefix + "drvDit", 0.0)
    oldTon = field_float(el, prefix + "drvTon", 20000.0)
    oldBit = field_float(el, prefix + "drvBits", 16.0)
    oldRte = field_float(el, prefix + "drvRate", 48000.0)
    oldEqM = field_float(el, prefix + "eqMidGain", 0.0)
    oldEqL = field_float(el, prefix + "eqLowGain", 0.0)
    oldEqH = field_float(el, prefix + "eqHighGain", 0.0)

    actual = [0.0, 0.0, 0.0, 0.0]
    if algo in (1, 2, 3, 5, 10):
        actual[0] = oldDrv
        actual[1] = oldOut
        actual[3] = oldTon
    elif algo == 4:
        actual[0] = oldBit
        actual[1] = oldRte
        actual[2] = oldDit
        actual[3] = oldTon
    elif algo == 6:
        # Prefer dedicated EQ fields if present; else fall back to pre-#597
        # packed encoding (Low/High packed into drvDrv/drvDit as 0..100).
        actual[0] = oldEqL if el.get(prefix + "eqLowGain") is not None \
                           else (oldDrv / 100.0 * 36.0 - 18.0)
        actual[1] = oldEqM
        actual[2] = oldTon
        actual[3] = oldEqH if el.get(prefix + "eqHighGain") is not None \
                           else (oldDit / 100.0 * 36.0 - 18.0)
    elif algo in (7, 8):
        actual[0] = oldDrv
        actual[1] = oldOut
        actual[2] = oldDit
        actual[3] = oldTon
    elif algo == 9:
        actual[0] = oldDrv
        actual[3] = oldTon
    elif algo == 11:
        actual[0] = oldDrv
        actual[1] = oldBit
        actual[2] = oldDit
        actual[3] = oldTon
    elif algo in (12, 13):
        actual[0] = oldDrv
        actual[1] = (oldOut + 24.0) * 0.25
        actual[2] = oldDit
        actual[3] = oldBit

    for s in range(4):
        el.set(prefix + f"insP{s+1}", f"{actual_to_norm(actual[s], algo, s):.6f}")

    # Strip the old field attributes — they're dead in v3.
    for old in ("drvDrv", "drvOut", "drvDit", "drvTon", "drvBits", "drvRate",
                "eqLowGain", "eqMidGain", "eqHighGain"):
        if (prefix + old) in el.attrib:
            del el.attrib[prefix + old]
    return True


def migrate_master_insert(el: ET.Element, slot: int) -> bool:
    """Apply v3 migration to one master insert slot inside a GlobalState element."""
    pfx = "mst_ins" if slot == 1 else "mst_ins2"
    char_name = el.get(pfx + "Char")
    if char_name is None or char_name not in ALGO_BY_NAME:
        return False
    if el.get(pfx + "P1") is not None:
        return False

    algo = ALGO_BY_NAME[char_name]
    oldDrv = field_float(el, pfx + "Drv", 0.0)
    oldOut = field_float(el, pfx + "Out", 0.0)
    oldDit = field_float(el, pfx + "Dit", 0.0)
    oldTon = field_float(el, pfx + "Ton", 20000.0)
    oldBit = field_float(el, pfx + "Bits", 16.0)
    oldRte = field_float(el, pfx + "Rate", 48000.0)
    oldEqM = field_float(el, pfx + "Mid", 0.0)
    oldEqL = field_float(el, pfx + "Low", 0.0)
    oldEqH = field_float(el, pfx + "High", 0.0)

    actual = [0.0, 0.0, 0.0, 0.0]
    if algo in (1, 2, 3, 5, 10):
        actual[0] = oldDrv; actual[1] = oldOut; actual[3] = oldTon
    elif algo == 4:
        actual[0] = oldBit; actual[1] = oldRte; actual[2] = oldDit; actual[3] = oldTon
    elif algo == 6:
        actual[0] = oldEqL if el.get(pfx + "Low") is not None  else (oldDrv / 100.0 * 36.0 - 18.0)
        actual[1] = oldEqM
        actual[2] = oldTon
        actual[3] = oldEqH if el.get(pfx + "High") is not None else (oldDit / 100.0 * 36.0 - 18.0)
    elif algo in (7, 8):
        actual[0] = oldDrv; actual[1] = oldOut; actual[2] = oldDit; actual[3] = oldTon
    elif algo == 9:
        actual[0] = oldDrv; actual[3] = oldTon
    elif algo == 11:
        actual[0] = oldDrv; actual[1] = oldBit; actual[2] = oldDit; actual[3] = oldTon
    elif algo in (12, 13):
        actual[0] = oldDrv; actual[1] = (oldOut + 24.0) * 0.25; actual[2] = oldDit; actual[3] = oldBit

    for s in range(4):
        el.set(pfx + f"P{s+1}", f"{actual_to_norm(actual[s], algo, s):.6f}")

    for old in ("Drv", "Out", "Dit", "Ton", "Bits", "Rate", "Mid", "Low", "High"):
        if (pfx + old) in el.attrib:
            del el.attrib[pfx + old]
    return True


def translate_mod_dest(old_dest: str, algo: int) -> str:
    """Translate an old <Asgn dest=...> ID to the new insert.p* slot."""
    generic = {"insert.drive": 0, "insert.output": 1,
               "insert.dither": 2, "insert.lpf": 3}
    if algo in (1, 2, 3, 5, 7, 8, 10):
        if old_dest in generic:
            return f"insert.p{generic[old_dest] + 1}"
    if algo == 4:
        m = {"insert.bits": 1, "insert.rate": 2, "insert.dither": 3, "insert.lpf": 4}
        if old_dest in m:
            return f"insert.p{m[old_dest]}"
    if algo == 6:
        m = {"insert.drive": 1, "insert.lpf": 3, "insert.dither": 4}
        if old_dest in m:
            return f"insert.p{m[old_dest]}"
    if algo == 9:
        m = {"insert.drive": 1, "insert.lpf": 4}
        if old_dest in m:
            return f"insert.p{m[old_dest]}"
    if algo == 11:
        m = {"ks.note": 1, "ks.octave": 2, "insert.dither": 3, "insert.lpf": 4}
        if old_dest in m:
            return f"insert.p{m[old_dest]}"
    if algo in (12, 13):
        m = {"insert.drive": 1, "voc.unison": 2, "voc.octave": 3, "voc.note": 4}
        if old_dest in m:
            return f"insert.p{m[old_dest]}"
    return ""   # not translatable — caller leaves as-is (will be dropped by loader)


def migrate_modulators(mods_el: ET.Element, algo: int) -> int:
    """Translate old <Asgn dest=...> values inside a <Modulators> child."""
    if mods_el is None or algo < 0:
        return 0
    changed = 0
    for asgn in mods_el.findall("Asgn"):
        dest = asgn.get("dest")
        if dest is None:
            continue
        new = translate_mod_dest(dest, algo)
        if new and new != dest:
            asgn.set("dest", new)
            changed += 1
    return changed


def migrate_file(path: Path, dry_run: bool) -> bool:
    """Migrate one .muRhyth or .muclid file. Returns True if anything changed."""
    tree = ET.parse(path)
    root = tree.getroot()
    changed = False

    if root.tag == "MuClidRhythm":
        # Single rhythm preset — fields use the "r0_" prefix on the root.
        rhythm_changed = migrate_insert(root, "r0_")
        algo = ALGO_BY_NAME.get(root.get("r0_drvChar", ""), -1)
        mods = root.find("Modulators")
        mod_changes = migrate_modulators(mods, algo)
        changed = rhythm_changed or mod_changes > 0
    elif root.tag == "MuClidPreset":
        # Full preset — iterate every <Rhythm> child (no prefix on its fields)
        # plus the <GlobalState> child for master inserts.
        for rhythm in root.findall("Rhythm"):
            r_changed = migrate_insert(rhythm, "")
            algo = ALGO_BY_NAME.get(rhythm.get("drvChar", ""), -1)
            mods = rhythm.find("Modulators")
            mod_changes = migrate_modulators(mods, algo)
            changed = changed or r_changed or mod_changes > 0
        for gs in root.findall("GlobalState"):
            gs_changed_1 = migrate_master_insert(gs, 1)
            gs_changed_2 = migrate_master_insert(gs, 2)
            changed = changed or gs_changed_1 or gs_changed_2
    else:
        return False

    if not changed:
        return False
    if dry_run:
        print(f"  [dry-run] would migrate {path.name}")
        return True

    # Back up the original (only if no .v2_backup yet — leave the first backup intact).
    backup = path.with_suffix(path.suffix + ".v2_backup")
    if not backup.exists():
        shutil.copy2(path, backup)

    tree.write(path, encoding="utf-8", xml_declaration=True)
    print(f"  migrated {path.name}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("preset_dir", type=Path)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not args.preset_dir.is_dir():
        print(f"error: {args.preset_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    files = list(args.preset_dir.rglob("*.muRhyth")) \
          + list(args.preset_dir.rglob("*.muclid"))
    files.sort()

    print(f"Scanning {len(files)} preset file(s) under {args.preset_dir}")
    n_changed = 0
    for f in files:
        if migrate_file(f, args.dry_run):
            n_changed += 1
    action = "would migrate" if args.dry_run else "migrated"
    print(f"{action} {n_changed} of {len(files)} file(s).")


if __name__ == "__main__":
    main()
