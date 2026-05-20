"""
Upgrade mu-clid .muclid preset files from v0 (JUCE-normalised 0..1 values)
to v2 (actual de-normalised values + string algorithm names).

Matches the logic in the old readParamPropertyAsActual v0 branch:
  1. Read normalised value from attribute
  2. Apply migrateLegacyPresetNorm for v0 (drvChar / drvBits range-expansion)
  3. Convert from 0-1 to actual via the current APVTS parameter ranges

Run with: python tools/upgrade_presets_v0_to_v2.py
"""

import xml.etree.ElementTree as ET
import math
import os
import sys
import shutil

# ── JUCE parameter conversion formulae ──────────────────────────────────────

def adsr_from_norm(norm):
    """JUCE NormalisableRange(0, 10, 0, skew=0.3): convertFrom0to1.
    JUCE formula: proportion = exp(log(proportion)/skew) = pow(norm, 1/skew)
    So actual = 10 * pow(norm, 1/0.3) = 10 * pow(norm, 3.333)"""
    n = float(norm)
    if n <= 0:
        return 0.0
    return 10.0 * (n ** (1.0 / 0.3))

def flt_cutoff_from_norm(norm):
    """JUCE NormalisableRange(20, 20000, 0, skew=0.25): convertFrom0to1.
    actual = 20 + 19980 * pow(norm, 1/0.25) = 20 + 19980 * pow(norm, 4)"""
    n = float(norm)
    if n <= 0:
        return 20.0
    return 20.0 + 19980.0 * (n ** (1.0 / 0.25))

def int_from_norm(norm, mn, mx):
    """JUCE AudioParameterInt convertFrom0to1: round(min + norm*(max-min))"""
    return int(round(mn + float(norm) * (mx - mn)))

def linear_from_norm(norm, mn, mx):
    """JUCE AudioParameterFloat linear: min + norm*(max-min)"""
    return mn + float(norm) * (mx - mn)

def bool_str(norm):
    return "true" if float(norm) >= 0.5 else "false"

def fmt(v):
    """Format a numeric value for XML output."""
    if isinstance(v, int):
        return str(v)
    f = float(v)
    if f == int(f) and abs(f) < 1e9:
        return str(int(f))
    return f"{f:.8g}"

# ── v0 → v1 migrations (algorithm range expansions, #430) ───────────────────

def migrate_drv_char(norm):
    """drvChar: old range 0..10, new range 0..12. newNorm = oldNorm*(10/12)"""
    return max(0.0, min(1.0, float(norm) * (10.0 / 12.0)))

def migrate_drv_bits(norm):
    """drvBits: old range 1..16, new range 0..16. newNorm = (1+oldNorm*15)/16"""
    return max(0.0, min(1.0, (1.0 + float(norm) * 15.0) / 16.0))

# ── Algorithm name tables (must match AlgorithmNames.h) ─────────────────────

INSERT_NAMES  = ["None","SoftClip","HardClip","Fold","Bitcrusher","Clipper","EQ",
                 "Compressor","Limiter","RingMod","TapeSat","Karplus","Vocoder","VocoderSt"]
FILTER_NAMES  = ["LP12","HP12","BP12","Notch12","LP24","HP24","BP24","LP6",
                 "CombPlus","AP12","Notch24","HP6","Peak","LowShelf","HighShelf","CombMinus"]
LOGIC_NAMES   = ["OR","AND","XOR","AOnly","BOnly"]
EFFECT_NAMES  = ["chorus","flanger","phaser","echo"]
REVERB_NAMES  = ["room","hall","plate","spring"]

def name_at(table, idx):
    if 0 <= idx < len(table):
        return table[idx]
    return str(idx)

# ── Rhythm element attribute conversion ─────────────────────────────────────

RHYTHM_REMOVED = {'drvAa', 'fltGain'}

def convert_rhythm_attr(attr_name, raw):
    """Return (new_value_str, keep:bool). keep=False to drop the attribute."""
    # Structural attrs: pass through unchanged
    if attr_name in ('name', 'colour', 'sample', 'sampleData', 'sampleName'):
        return raw, True
    # Obsolete attrs: remove
    if attr_name in RHYTHM_REMOVED:
        return None, False
    # Channel strip: stored normalised in both v0 and v2 (loader does setValueNotifyingHost(norm))
    if attr_name.startswith('ch_'):
        return raw, True

    n = float(raw)

    # HitGen A / B / C
    for L in ('A', 'B', 'C'):
        if attr_name == f'steps{L}':       return fmt(int_from_norm(n, 1, 64)), True
        if attr_name == f'hits{L}':        return fmt(int_from_norm(n, 0, 64)), True
        if attr_name == f'rot{L}':         return fmt(int_from_norm(n, -32, 32)), True
        if attr_name == f'prePad{L}':      return fmt(int_from_norm(n, 0, 12)), True
        if attr_name == f'postPad{L}':     return fmt(int_from_norm(n, 0, 12)), True
        if attr_name == f'insSt{L}':       return fmt(int_from_norm(n, 0, 63)), True
        if attr_name == f'insLen{L}':      return fmt(int_from_norm(n, 0, 8)), True
        if attr_name == f'insMode{L}':     return bool_str(n), True
        if attr_name == f'prePadMode{L}':  return bool_str(n), True
        if attr_name == f'postPadMode{L}': return bool_str(n), True

    # Rhythm-level sequencer params
    if attr_name == 'logic':
        return name_at(LOGIC_NAMES, int_from_norm(n, 0, 4)), True
    if attr_name == 'patLeg':   return bool_str(n), True

    # Pitch
    if attr_name == 'pitchOct':  return fmt(int_from_norm(n, -4, 4)), True
    if attr_name == 'pitchSemi': return fmt(int_from_norm(n, -12, 12)), True
    if attr_name == 'pitchFine': return fmt(linear_from_norm(n, -100, 100)), True

    # Pitch envelope
    if attr_name in ('pEnvAtk', 'pEnvDec', 'pEnvRel'):
        return fmt(adsr_from_norm(n)), True
    if attr_name == 'pEnvSus': return fmt(linear_from_norm(n, 0, 100)), True
    if attr_name == 'pEnvDep': return fmt(linear_from_norm(n, 0, 24)), True
    if attr_name == 'pEnvLeg': return bool_str(n), True

    # Filter
    if attr_name == 'fltType':
        return name_at(FILTER_NAMES, int_from_norm(n, 0, 15)), True
    if attr_name == 'fltCut': return fmt(flt_cutoff_from_norm(n)), True
    if attr_name == 'fltRes': return fmt(linear_from_norm(n, 0, 0.99)), True

    # Filter envelope
    if attr_name in ('fEnvAtk', 'fEnvDec', 'fEnvRel'):
        return fmt(adsr_from_norm(n)), True
    if attr_name == 'fEnvSus': return fmt(linear_from_norm(n, 0, 100)), True
    if attr_name == 'fEnvDep': return fmt(linear_from_norm(n, 0, 48)), True
    if attr_name == 'fEnvLeg': return bool_str(n), True

    # Amp
    if attr_name == 'ampLvl': return fmt(linear_from_norm(n, 0, 2)), True
    if attr_name in ('aEnvAtk', 'aEnvDec'):
        return fmt(adsr_from_norm(n)), True
    if attr_name == 'aEnvSus': return fmt(linear_from_norm(n, 0, 100)), True
    if attr_name == 'aEnvRel':
        # norm=1.0 is the "play to end" sentinel → 10.0 s
        if n >= 1.0 - 1e-6:
            return "10", True
        return fmt(adsr_from_norm(n)), True
    if attr_name == 'aEnvLeg': return bool_str(n), True
    if attr_name == 'accentDb': return fmt(linear_from_norm(n, 0, 12)), True

    # Insert / drive (with v0 → v1 range migration)
    if attr_name == 'drvChar':
        mn = migrate_drv_char(n)
        return name_at(INSERT_NAMES, int_from_norm(mn, 0, 12)), True
    if attr_name == 'drvDrv':  return fmt(linear_from_norm(n, 0, 100)), True
    if attr_name == 'drvOut':  return fmt(linear_from_norm(n, -24, 24)), True
    if attr_name == 'drvBits':
        mn = migrate_drv_bits(n)
        return fmt(linear_from_norm(mn, 0, 16)), True
    if attr_name == 'drvRate': return fmt(linear_from_norm(n, 100, 48000)), True
    if attr_name == 'drvDit':  return fmt(linear_from_norm(n, 0, 100)), True
    if attr_name == 'drvTon':  return fmt(linear_from_norm(n, 20, 20000)), True
    if attr_name == 'eqMidGain': return fmt(linear_from_norm(n, -18, 18)), True

    # Unknown attr: pass through
    return raw, True

# ── GlobalState attribute conversion ─────────────────────────────────────────

# Global bool params (stored as 0.0/1.0 normalised in v0, "true"/"false" in v2)
GLOBAL_BOOLS = {
    'eff_en', 'dly_en', 'dly_mode', 'dly_syncDot', 'dly_syncTrip',
    'rev_en', 'echo_en', 'echo_syncDot', 'echo_syncTrip',
    'ret_eff_mute', 'ret_eff_solo', 'ret_dly_mute', 'ret_dly_solo',
    'ret_rev_mute', 'ret_rev_solo',
}

def convert_global_attr(name, raw):
    n = float(raw)

    # Algorithm indices → name strings
    if name == 'eff_algo':
        return name_at(EFFECT_NAMES, int_from_norm(n, 0, 7))
    if name == 'rev_algo':
        return name_at(REVERB_NAMES, int_from_norm(n, 0, 3))
    if name in ('mst_insChar', 'mst_ins2Char'):
        mn = migrate_drv_char(n)
        return name_at(INSERT_NAMES, int_from_norm(mn, 0, 12))

    # Bools
    if name in GLOBAL_BOOLS:
        return bool_str(n)

    # Ints
    if name == 'dly_syncDenom':  return fmt(int_from_norm(n, 0, 3))
    if name == 'dly_count':      return fmt(int_from_norm(n, 1, 8))
    if name == 'echo_syncDenom': return fmt(int_from_norm(n, 0, 3))
    if name == 'echo_count':     return fmt(int_from_norm(n, 1, 8))
    if name in ('ret_eff_scSrc','ret_dly_scSrc','ret_rev_scSrc'):
        return fmt(int_from_norm(n, 0, 8))
    if name == 'mstrLoop':       return fmt(int_from_norm(n, 0, 16))

    # Floats with explicit ranges
    if name == 'eff_send': return fmt(linear_from_norm(n, 0, 1))
    if name in ('eff_p0','eff_p1','eff_p2','eff_p3','eff_p4'):
        return fmt(linear_from_norm(n, 0, 1))
    if name == 'dly_ms':     return fmt(linear_from_norm(n, 1, 4000))
    if name == 'dly_fb':     return fmt(linear_from_norm(n, 0, 0.98))
    if name == 'dly_spread': return fmt(linear_from_norm(n, 0, 1))
    if name == 'dly_dirt':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'dly_send':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_lvl':    return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_size':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_pre':    return fmt(linear_from_norm(n, 0, 100))
    if name == 'rev_diff':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_damp':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_mod':    return fmt(linear_from_norm(n, 0, 1))
    if name == 'rev_dirt':   return fmt(linear_from_norm(n, 0, 1))
    if name in ('eff2dly','eff2rev','dly2rev'):
        return fmt(linear_from_norm(n, 0, 1))
    if name == 'echo_mode':   return fmt(linear_from_norm(n, 0, 1))
    if name == 'echo_ms':     return fmt(linear_from_norm(n, 1, 4000))
    if name == 'echo_fb':     return fmt(linear_from_norm(n, 0, 1))
    if name == 'echo_spread': return fmt(linear_from_norm(n, 0, 1))
    if name == 'echo_dirt':   return fmt(linear_from_norm(n, 0, 1))
    if name in ('ret_eff_lvl','ret_dly_lvl','ret_rev_lvl'):
        return fmt(linear_from_norm(n, 0, 1))
    if name in ('ret_eff_pan','ret_dly_pan','ret_rev_pan'):
        return fmt(linear_from_norm(n, -1, 1))
    if name in ('ret_eff_scAmt','ret_dly_scAmt','ret_rev_scAmt'):
        return fmt(linear_from_norm(n, 0, 1))
    if name in ('ret_eff_scAtk','ret_dly_scAtk','ret_rev_scAtk'):
        return fmt(linear_from_norm(n, 1, 500))
    if name in ('ret_eff_scRel','ret_dly_scRel','ret_rev_scRel'):
        return fmt(linear_from_norm(n, 10, 2000))
    if name == 'mstr_lvl': return fmt(linear_from_norm(n, 0, 1))
    if name == 'mstr_pan': return fmt(linear_from_norm(n, -1, 1))
    if name in ('mst_insDrv','mst_ins2Drv'):
        return fmt(linear_from_norm(n, 0, 100))
    if name in ('mst_insOut','mst_ins2Out'):
        return fmt(linear_from_norm(n, -24, 24))
    if name in ('mst_insBits','mst_ins2Bits'):
        mn = migrate_drv_bits(n)
        return fmt(linear_from_norm(mn, 0, 16))
    if name in ('mst_insRate','mst_ins2Rate'):
        return fmt(linear_from_norm(n, 100, 48000))
    if name in ('mst_insDit','mst_ins2Dit'):
        return fmt(linear_from_norm(n, 0, 100))
    if name in ('mst_insTon','mst_ins2Ton'):
        return fmt(linear_from_norm(n, 20, 20000))
    if name in ('mst_insMid','mst_ins2Mid'):
        return fmt(linear_from_norm(n, -18, 18))

    # Default: pass through unchanged
    return raw

# ── Main upgrade logic ────────────────────────────────────────────────────────

def upgrade_preset(path):
    print(f"Reading {os.path.basename(path)} ...", end=' ')

    tree = ET.parse(path)
    root = tree.getroot()

    if root.tag != 'MuClidPreset':
        print(f"SKIP (root tag is {root.tag!r})")
        return False

    version = int(root.get('presetVersion', '0'))
    if version >= 2:
        print(f"SKIP (already v{version})")
        return False

    # Stamp v2
    root.set('presetVersion', '2')

    # Upgrade each Rhythm child
    for rhythm in root.findall('Rhythm'):
        new_attribs = {}
        for k, v in list(rhythm.attrib.items()):
            new_v, keep = convert_rhythm_attr(k, v)
            if keep:
                new_attribs[k] = new_v

        # Add sampleName if embedded sampleData present but sampleName absent
        if 'sampleData' in new_attribs and 'sampleName' not in new_attribs:
            sample_path = new_attribs.get('sample', '')
            if sample_path:
                new_attribs['sampleName'] = os.path.basename(sample_path)

        rhythm.attrib.clear()
        rhythm.attrib.update(new_attribs)

    # Upgrade GlobalState child
    for gs in root.findall('GlobalState'):
        new_attribs = {}
        for k, v in list(gs.attrib.items()):
            new_attribs[k] = convert_global_attr(k, v)
        gs.attrib.clear()
        gs.attrib.update(new_attribs)

    # Back up original
    backup = path + '.v0_backup'
    shutil.copy2(path, backup)

    # Write upgraded file
    ET.register_namespace('', '')
    tree.write(path, encoding='unicode', xml_declaration=True)

    print(f"UPGRADED (backup: {os.path.basename(backup)})")
    return True


# ── .muRhyth upgrade (MuClidRhythm root, r0_-prefixed rhythm attrs) ──────────

def upgrade_rhythm_preset(path):
    print(f"Reading {os.path.basename(path)} ...", end=' ')

    tree = ET.parse(path)
    root = tree.getroot()

    if root.tag != 'MuClidRhythm':
        print(f"SKIP (root tag is {root.tag!r})")
        return False

    version = int(root.get('presetVersion', '0'))
    if version >= 2:
        print(f"SKIP (already v{version})")
        return False

    new_attribs = {'presetVersion': '2'}
    for k, v in root.attrib.items():
        if k == 'presetVersion':
            continue  # replaced by '2' above
        if k.startswith('r0_'):
            suffix = k[3:]
            new_v, keep = convert_rhythm_attr(suffix, v)
            if keep:
                new_attribs[k] = new_v
        else:
            new_attribs[k] = v  # presetName, presetCategory, ch_* pass through unchanged

    root.attrib.clear()
    root.attrib.update(new_attribs)

    backup = path + '.v0_backup'
    shutil.copy2(path, backup)

    ET.register_namespace('', '')
    tree.write(path, encoding='unicode', xml_declaration=True)

    print(f"UPGRADED (backup: {os.path.basename(backup)})")
    return True


if __name__ == '__main__':
    preset_dir = r"D:\OneDrive\Documents\TDP\muClid\Presets"
    rhythm_dir = r"D:\OneDrive\Documents\TDP\muClid\Rhythms"
    if len(sys.argv) > 1:
        preset_dir = sys.argv[1]
        rhythm_dir = sys.argv[1]

    upgraded = 0

    muclid_files = [f for f in os.listdir(preset_dir) if f.endswith('.muclid')]
    for f in sorted(muclid_files):
        if upgrade_preset(os.path.join(preset_dir, f)):
            upgraded += 1

    murhyth_files = [f for f in os.listdir(rhythm_dir) if f.endswith('.muRhyth')]
    for f in sorted(murhyth_files):
        if upgrade_rhythm_preset(os.path.join(rhythm_dir, f)):
            upgraded += 1

    total = len(muclid_files) + len(murhyth_files)
    if total == 0:
        print("No .muclid or .muRhyth files found.")
        sys.exit(1)

    print(f"\nDone: {upgraded}/{total} files upgraded.")
