#!/usr/bin/env python3
# Validates the generated mu-tant factory presets against the serialised schema:
#   - every file is well-formed XML with the right root tag
#   - full presets carry EXACTLY the complete APVTS param-id set (no typo/missing)
#   - voice presets carry exactly the 44 per-voice base ids, all v in [0,1]
#   - every modulation Asgn targets a valid destination id
#   - gate envelope start cells fall inside the 32-cell (2-bar / 1-16) grid
import glob, math, os, sys
import xml.etree.ElementTree as ET

FACTORY = r"D:\Andy Tuke\Music - Music Software\TDP\Presets\muTant"

VOICE_BASE = [
 "o1_oct","o1_semi","o1_fine","o1_pos","o2_oct","o2_semi","o2_fine","o2_pos",
 "o1_wt","o2_wt","xmod_phaseMode","xmod_index","sync","xmod_fdbk","xmod_ampMode",
 "xmod_depth","xmod_ssb","o1_lvl","o2_lvl","noise_lvl","noise_type","flt_type",
 "flt_cut","flt_res","flt_drv","flt_lo_cut","flt_env_depth","flt2_type","flt2_cut",
 "flt2_res","flt2_drv","flt2_lo_cut","flt2_env_depth","flt_series","o1_penv_depth",
 "o2_penv_depth","level","gate_gap","gate_bypass","drvChar","insP1","insP2","insP3","insP4"]

CH_KEYS = [f"ch{i}_{s}" for i in range(8) for s in
           ("lvl","pan","mute","solo","sendEff","sendDly","sendRev","scSrc","scAmt","scAtk","scRel","outBus")]

FX_KEYS = ["eff_algo","eff_en","eff_p0","eff_p1","eff_p2","eff_p3","eff_p4",
 "dly_en","dly_mode","dly_ms","dly_syncDenom","dly_syncDot","dly_syncTrip","dly_count","dly_fb","dly_spread","dly_dirt",
 "rev_algo","rev_en","rev_lvl","rev_size","rev_pre","rev_diff","rev_damp","rev_mod","rev_dirt",
 "eff2dly","eff2rev","dly2rev",
 "echo_en","echo_mode","echo_ms","echo_syncDenom","echo_syncDot","echo_syncTrip","echo_count","echo_fb","echo_spread","echo_dirt",
 "mstr_lvl","mstr_pan","mst_insChar","mst_insP1","mst_insP2","mst_insP3","mst_insP4",
 "mst_ins2Char","mst_ins2P1","mst_ins2P2","mst_ins2P3","mst_ins2P4"]

EXPECTED_FULL = set(["root","scale","mstrLoop"]) \
    | {f"v{v}_{b}" for v in range(8) for b in VOICE_BASE} | set(CH_KEYS) | set(FX_KEYS)

VALID_DESTS = {"osc1.octave","osc1.semi","osc1.fine","osc1.pos","osc2.octave","osc2.semi",
 "osc2.fine","osc2.pos","xmod.index","xmod.depth","xmod.ssb","osc1.level","osc2.level",
 "noise.level","filter.cutoff","filter.resonance","filter2.cutoff.prop","filter2.resonance.prop",
 "level","insert.p1","insert.p2","insert.p3","insert.p4"}

errors = []

def check_asgns_and_gates(root, fname):
    for asg in root.iter("Asgn"):
        d = asg.get("dest")
        if d not in VALID_DESTS:
            errors.append(f"{fname}: invalid Asgn dest '{d}'")
    for tag in ("Gate", "FilterGate", "PitchGate"):
        for g in root.iter(tag):
            for e in g.findall("Env"):
                if int(e.get("start")) >= 32:
                    errors.append(f"{fname}: {tag} env start {e.get('start')} >= 32")

# ── Voice presets ────────────────────────────────────────────────────────────
vfiles = glob.glob(os.path.join(FACTORY, "Voices", "*.muPattern"))
for f in vfiles:
    fn = os.path.basename(f)
    try:
        root = ET.parse(f).getroot()
    except Exception as ex:
        errors.append(f"{fn}: XML parse error {ex}"); continue
    if root.tag != "MuTantVoice":
        errors.append(f"{fn}: root tag {root.tag}"); continue
    ids = [p.get("id") for p in root.findall("p")]
    if sorted(ids) != sorted(VOICE_BASE):
        miss = set(VOICE_BASE) - set(ids); extra = set(ids) - set(VOICE_BASE)
        errors.append(f"{fn}: p-id mismatch missing={miss} extra={extra}")
    for p in root.findall("p"):
        val = float(p.get("v"))
        if not (0.0 - 1e-9 <= val <= 1.0 + 1e-9) or math.isnan(val):
            errors.append(f"{fn}: {p.get('id')} normalised v={val} out of [0,1]")
    if len(root.findall("Modulators/Seq")) != 8:
        errors.append(f"{fn}: expected 8 Seq, got {len(root.findall('Modulators/Seq'))}")
    check_asgns_and_gates(root, fn)

# ── Full presets (factory pack — only the generated ones; skip legacy) ────────
ffiles = glob.glob(os.path.join(FACTORY, "Presets", "*.muTant"))
checked_full = 0
for f in ffiles:
    fn = os.path.basename(f)
    try:
        root = ET.parse(f).getroot()
    except Exception as ex:
        errors.append(f"{fn}: XML parse error {ex}"); continue
    if root.tag != "MuTantPreset":
        errors.append(f"{fn}: root tag {root.tag}"); continue
    state = root.find("MuTantState")
    if state is None:
        errors.append(f"{fn}: no MuTantState"); continue
    ids = [p.get("id") for p in state.findall("PARAM")]
    idset = set(ids)
    # Only enforce the full schema on freshly generated presets (they have no
    # legacy unprefixed params). Legacy demo presets are skipped here.
    if "v0_xmod_index" not in idset:
        continue
    checked_full += 1
    if idset != EXPECTED_FULL:
        miss = EXPECTED_FULL - idset; extra = idset - EXPECTED_FULL
        errors.append(f"{fn}: PARAM-id mismatch missing={sorted(miss)[:6]} extra={sorted(extra)[:6]}")
    for p in state.findall("PARAM"):
        try:
            v = float(p.get("value"))
        except (TypeError, ValueError):
            errors.append(f"{fn}: {p.get('id')} non-numeric value {p.get('value')}"); continue
        if math.isnan(v) or math.isinf(v):
            errors.append(f"{fn}: {p.get('id')} non-finite")
    nvoices = int(state.get("numVoices"))
    voice_nodes = state.findall("VoiceData/Voice")
    if len(voice_nodes) != nvoices:
        errors.append(f"{fn}: numVoices {nvoices} != {len(voice_nodes)} VoiceData/Voice")
    check_asgns_and_gates(root, fn)

print(f"Voice presets checked: {len(vfiles)}")
print(f"Full presets checked (generated): {checked_full}")
if errors:
    print(f"\n{len(errors)} ERRORS:")
    for e in errors[:40]:
        print("  " + e)
    sys.exit(1)
print("\nAll generated presets valid.")
