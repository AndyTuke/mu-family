#!/usr/bin/env python3
# mu-tant factory-preset generator.
#
# Emits 100 single-voice presets (.muPattern) + 100 full presets (.muTant) that
# match mu-tant's CURRENT serialised schema exactly:
#   - .muPattern  = <MuTantVoice> with one <p id="base" v="NORMALISED 0..1"/> per
#                   per-voice param (v{N}_ prefix stripped) + <Modulators>/<Gate>/
#                   <FilterGate>/<PitchGate>.  (saveVoicePreset: rp->getValue())
#   - .muTant     = <MuTantPreset name desc category><MuTantState numVoices
#                   voiceColours><PARAM id value=DENORMALISED/>...<VoiceData/>.
#
# Param ranges/skews mirror PluginProcessor_APVTS.cpp (addVoiceParams +
# createParameterLayout) and mu-core MixerFxParams.h. Two skewed params only:
#   flt(2)_cut  NormalisableRange(20,20000).setSkewForCentre(640)  -> skew 0.199608
#   flt(2)_lo_cut NormalisableRange(0,1000,0,0.35)                 -> skew 0.35
# Everything else is linear; ints/choices/bools normalise (v-min)/(max-min).
#
# Output lands in the factory pack (…/TDP/Presets/muTant/{Voices,Presets}); voice
# presets reference factory wavetables by index, so no wavetable files travel with
# them. Deterministic (seeded) so re-runs are reproducible.

import math
import os
import random
import xml.etree.ElementTree as ET
from xml.dom import minidom

SEED = 1981
random.seed(SEED)

# ── Output locations ─────────────────────────────────────────────────────────
FACTORY_ROOT = r"D:\Andy Tuke\Music - Music Software\TDP\Presets\muTant"
VOICES_DIR   = os.path.join(FACTORY_ROOT, "Voices")
PRESETS_DIR  = os.path.join(FACTORY_ROOT, "Presets")

# ── Wavetable indices (factory bank order, 0..24) ────────────────────────────
WT = {
    "basic": 0, "sine": 1, "triangle": 2, "saw": 3, "square": 4, "pwm": 5,
    "fm11": 6, "fm21": 7, "fm31": 8, "fm115": 9,
    "formantlo": 10, "formantsweep": 11, "formanthi": 12,
    "odd": 13, "even": 14, "harmpeak": 15, "organ": 16, "hollow": 17,
    "bell": 18, "saw2sine": 19, "sq2sine": 20, "harmstack": 21,
    "vocal": 22, "syncsaw": 23, "sparse": 24,
}

# ── Filter type indices ──────────────────────────────────────────────────────
FT = {"LP12": 0, "HP12": 1, "BP12": 2, "Notch12": 3, "LP24": 4, "HP24": 5,
      "BP24": 6, "LP6": 7, "CombPlus": 8, "AP12": 9, "Notch24": 10, "HP6": 11,
      "Peak": 12, "LowShelf": 13, "HighShelf": 14, "CombMinus": 15}

# ── Insert algo indices ──────────────────────────────────────────────────────
INS = {"None": 0, "SoftClip": 1, "HardClip": 2, "Fold": 3, "Bitcrusher": 4,
       "Clipper": 5, "EQ": 6, "Compressor": 7, "Limiter": 8, "RingMod": 9,
       "TapeSat": 10, "Karplus": 11, "Vocoder": 12, "VocoderSt": 13}

# ── Per-voice param spec: base_id -> (kind, lo, hi, skew, default) ────────────
#   kind: f=float linear, fc=float cutoff-skew, fl=float lo-cut-skew,
#         i=int, c=choice (hi=numChoices-1), b=bool
CUT_SKEW = math.log(0.5) / math.log((640.0 - 20.0) / (20000.0 - 20.0))   # 0.199608
LOCUT_SKEW = 0.35

VOICE_SPEC = {
    "o1_oct": ("i", -3, 3, None, 0),    "o1_semi": ("i", -12, 12, None, 0),
    "o1_fine": ("i", -100, 100, None, 0), "o1_pos": ("i", 0, 255, None, 0),
    "o2_oct": ("i", -3, 3, None, 0),    "o2_semi": ("i", -12, 12, None, 2),
    "o2_fine": ("i", -100, 100, None, 0), "o2_pos": ("i", 0, 255, None, 0),
    "o1_wt": ("i", 0, 24, None, 0),     "o2_wt": ("i", 0, 24, None, 0),
    "xmod_phaseMode": ("c", 0, 2, None, 1),
    "xmod_index": ("f", 0.0, 100.0, None, 0.0),
    "sync": ("b", 0, 1, None, 0),
    "xmod_fdbk": ("b", 0, 1, None, 0),
    "xmod_ampMode": ("c", 0, 2, None, 0),
    "xmod_depth": ("f", -100.0, 100.0, None, 0.0),
    "xmod_ssb": ("f", -2000.0, 2000.0, None, 0.0),
    "o1_lvl": ("f", -60.0, 6.0, None, 0.0),
    "o2_lvl": ("f", -60.0, 6.0, None, -6.0),
    "noise_lvl": ("f", -60.0, 6.0, None, -60.0),
    "noise_type": ("c", 0, 1, None, 0),
    "flt_type": ("i", 0, 15, None, 0),
    "flt_cut": ("fc", 20.0, 20000.0, CUT_SKEW, 8000.0),
    "flt_res": ("f", 0.0, 0.99, None, 0.2),
    "flt_drv": ("f", 0.0, 1.0, None, 0.0),
    "flt_lo_cut": ("fl", 0.0, 1000.0, LOCUT_SKEW, 0.0),
    "flt_env_depth": ("f", -1.0, 1.0, None, 1.0),
    "flt2_type": ("i", 0, 15, None, 0),
    "flt2_cut": ("fc", 20.0, 20000.0, CUT_SKEW, 8000.0),
    "flt2_res": ("f", 0.0, 0.99, None, 0.0),
    "flt2_drv": ("f", 0.0, 1.0, None, 0.0),
    "flt2_lo_cut": ("fl", 0.0, 1000.0, LOCUT_SKEW, 0.0),
    "flt2_env_depth": ("f", -1.0, 1.0, None, 0.0),
    "flt_series": ("b", 0, 1, None, 1),
    "o1_penv_depth": ("f", -24.0, 24.0, None, 0.0),
    "o2_penv_depth": ("f", -24.0, 24.0, None, 0.0),
    "level": ("f", -60.0, 6.0, None, -6.0),
    "gate_gap": ("f", 0.0, 100.0, None, 0.0),
    "gate_bypass": ("b", 0, 1, None, 0),
    "drvChar": ("i", 0, 13, None, 0),
    "insP1": ("f", 0.0, 1.0, None, 0.0), "insP2": ("f", 0.0, 1.0, None, 0.0),
    "insP3": ("f", 0.0, 1.0, None, 0.0), "insP4": ("f", 0.0, 1.0, None, 0.0),
}

# ── Channel-strip + global-FX param spec: id -> (kind, lo, hi, default) ───────
def ch_spec():
    s = {}
    for i in range(8):
        c = f"ch{i}_"
        s[c+"lvl"]=("f",0,1,1.0); s[c+"pan"]=("f",-1,1,0.0)
        s[c+"mute"]=("b",0,1,0); s[c+"solo"]=("b",0,1,0)
        s[c+"sendEff"]=("f",0,1,0.0); s[c+"sendDly"]=("f",0,1,0.0); s[c+"sendRev"]=("f",0,1,0.0)
        s[c+"scSrc"]=("i",0,9,0); s[c+"scAmt"]=("f",0,1,0.0)
        s[c+"scAtk"]=("f",1,500,5.0); s[c+"scRel"]=("f",10,2000,100.0); s[c+"outBus"]=("i",0,8,0)
    return s

FX_SPEC = {
    "eff_algo":("i",0,7,0), "eff_en":("b",0,1,1),
    "eff_p0":("f",0,1,0.5),"eff_p1":("f",0,1,0.5),"eff_p2":("f",0,1,0.5),"eff_p3":("f",0,1,0.5),"eff_p4":("f",0,1,0.5),
    "dly_en":("b",0,1,1),"dly_mode":("b",0,1,1),"dly_ms":("f",1,4000,250.0),"dly_syncDenom":("i",0,3,2),
    "dly_syncDot":("b",0,1,1),"dly_syncTrip":("b",0,1,0),"dly_count":("i",1,8,1),"dly_fb":("f",0,0.98,0.30),
    "dly_spread":("f",0,1,0.0),"dly_dirt":("f",0,1,0.0),
    "rev_algo":("i",0,3,1),"rev_en":("b",0,1,1),"rev_lvl":("f",0,1,1.0),"rev_size":("f",0,1,0.75),
    "rev_pre":("f",0,100,25.0),"rev_diff":("f",0,1,0.80),"rev_damp":("f",0,1,0.30),"rev_mod":("f",0,1,0.15),"rev_dirt":("f",0,1,0.0),
    "eff2dly":("f",0,1,0.0),"eff2rev":("f",0,1,0.0),"dly2rev":("f",0,1,0.0),
    "echo_en":("b",0,1,1),"echo_mode":("f",0,1,0.0),"echo_ms":("f",1,4000,250.0),"echo_syncDenom":("i",0,3,2),
    "echo_syncDot":("b",0,1,0),"echo_syncTrip":("b",0,1,0),"echo_count":("i",1,8,1),"echo_fb":("f",0,1,0.45),
    "echo_spread":("f",0,1,0.0),"echo_dirt":("f",0,1,0.0),
    "mstr_lvl":("f",0,1,1.0),"mstr_pan":("f",-1,1,0.0),
    "mst_insChar":("i",0,13,0),"mst_insP1":("f",0,1,0.0),"mst_insP2":("f",0,1,0.0),"mst_insP3":("f",0,1,0.0),"mst_insP4":("f",0,1,0.0),
    "mst_ins2Char":("i",0,13,0),"mst_ins2P1":("f",0,1,0.0),"mst_ins2P2":("f",0,1,0.0),"mst_ins2P3":("f",0,1,0.0),"mst_ins2P4":("f",0,1,0.0),
}
CH_SPEC = ch_spec()

# ── Normalisation (matches juce::NormalisableRange::convertTo0to1) ────────────
def normalise(base, value):
    kind, lo, hi, skew, _ = VOICE_SPEC[base]
    if kind == "b":
        return 1.0 if value else 0.0
    if kind == "c":
        return value / hi if hi else 0.0
    if kind == "i":
        return (value - lo) / (hi - lo)
    if kind == "f":
        return (value - lo) / (hi - lo)
    if kind == "fc":   # cutoff skew, lo=20 hi=20000
        return ((value - lo) / (hi - lo)) ** skew
    if kind == "fl":   # lo-cut skew, lo=0
        return (value / hi) ** skew if value > 0 else 0.0
    raise ValueError(kind)


def clampv(base, value):
    _, lo, hi, _, _ = VOICE_SPEC[base]
    return max(lo, min(hi, value))


# ── Recipe = one voice's character ───────────────────────────────────────────
class Voice:
    def __init__(self, name, category):
        self.name = name
        self.category = category
        self.p = {}                # base_id -> denormalised value (overrides default)
        self.mods = []             # list of (cs_id, mode, polarity, loopNV, points/steps)
        self.asgns = []            # list of (src, dest, depth)
        self.gate = []             # list of GateEnv dicts
        self.filtergate = []
        self.pitchgate = []

    def set(self, **kw):
        for k, v in kw.items():
            self.p[k] = clampv(k, v)
        return self

    def lfo(self, cs, dest, depth, loopNV="Whole", shape="sine"):
        # Smooth bipolar LFO curve → dest. shape ∈ sine/triangle/ramp/random.
        if shape == "sine":
            pts = [(0.0, 0.0), (0.25, 1.0), (0.5, 0.0), (0.75, -1.0), (1.0, 0.0)]
        elif shape == "triangle":
            pts = [(0.0, -1.0), (0.5, 1.0), (1.0, -1.0)]
        elif shape == "ramp":
            pts = [(0.0, -1.0), (0.999, 1.0), (1.0, -1.0)]
        else:  # random sample-hold-ish curve
            pts = [(round(i / 6, 3), round(random.uniform(-1, 1), 2)) for i in range(7)]
        self.mods.append((cs, "Smooth", "Bipolar", loopNV, pts))
        self.asgns.append((cs, dest, depth))
        return self

    def add_gate(self, envs, which="gate"):
        getattr(self, which).extend(envs)
        return self


def env(start, length, split=0.0, atk=0.0, dec=0.0, rev=0):
    return dict(start=start, len=length, split=split, atk=atk, dec=dec, rev=rev)


# ── Pretty XML writer ────────────────────────────────────────────────────────
def fnum(x):
    # Compact float like JUCE writes (drop trailing .0? JUCE keeps it; we mirror).
    if isinstance(x, float) and x.is_integer():
        return str(x)            # e.g. "8000.0"
    return repr(float(x))


def write_xml(root, path):
    raw = ET.tostring(root, encoding="unicode")
    dom = minidom.parseString(raw)
    pretty = dom.toprettyxml(indent="  ")
    # minidom emits its own header; ensure UTF-8 declaration matches JUCE style.
    lines = [ln for ln in pretty.split("\n") if ln.strip()]
    if lines[0].startswith("<?xml"):
        lines[0] = '<?xml version="1.0" encoding="UTF-8"?>'
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def modulators_xml(parent, voice):
    mods = ET.SubElement(parent, "Modulators")
    active = {m[0]: m for m in voice.mods}
    for i in range(8):
        cid = f"cs{i}"
        if cid in active:
            _, mode, pol, loopNV, pts = active[cid]
            seq = ET.SubElement(mods, "Seq", id=cid, mode=mode, polarity=pol,
                                loopNV=loopNV, loopMod="None", loopMult="1",
                                stepNV="Sixteenth", stepMod="None", stepMult="1")
            for (x, y) in pts:
                ET.SubElement(seq, "Point", x=fnum(x), y=fnum(y), bez="0", hx="0.0", hy="0.0")
        else:
            ET.SubElement(mods, "Seq", id=cid, mode="Stepped", polarity="Bipolar",
                          loopNV="Whole", loopMod="None", loopMult="1",
                          stepNV="Sixteenth", stepMod="None", stepMult="1")
    for n, (src, dest, depth) in enumerate(voice.asgns):
        ET.SubElement(mods, "Asgn", id=f"asg{n}", src=src, dest=dest,
                      depth=fnum(depth), curve="0.0")


def gate_xml(parent, tag, envs):
    g = ET.SubElement(parent, tag, subdiv="16", bars="2")
    for e in envs:
        ET.SubElement(g, "Env", start=str(e["start"]), len=str(e["len"]),
                      split=fnum(e["split"]), atk=fnum(e["atk"]),
                      dec=fnum(e["dec"]), rev=str(e["rev"]))


# ── .muPattern (single voice) ────────────────────────────────────────────────
def write_voice_preset(voice):
    root = ET.Element("MuTantVoice")
    for base in VOICE_SPEC:
        val = voice.p.get(base, VOICE_SPEC[base][4])
        ET.SubElement(root, "p", id=base, v=repr(normalise(base, val)))
    modulators_xml(root, voice)
    gate_xml(root, "Gate", voice.gate)
    gate_xml(root, "FilterGate", voice.filtergate)
    gate_xml(root, "PitchGate", voice.pitchgate)
    safe = voice.name.replace("/", "_").replace("\\", "_")
    write_xml(root, os.path.join(VOICES_DIR, safe + ".muPattern"))


# ── .muTant (full preset) ────────────────────────────────────────────────────
def write_full_preset(name, desc, category, voices, mixer, fx):
    root = ET.Element("MuTantPreset", name=name, description=desc, category=category)
    n = len(voices)
    colours = ",".join(str(i % 8) for i in range(8))
    state = ET.SubElement(root, "MuTantState", numVoices=str(n), voiceColours=colours)

    def param(pid, value):
        ET.SubElement(state, "PARAM", id=pid, value=fnum(value))

    # Globals.
    param("root", float(mixer.get("root", 9)))     # default A
    param("scale", float(mixer.get("scale", 0)))
    param("mstrLoop", float(mixer.get("mstrLoop", 0)))

    # Per-voice params (all 8 slots; inactive slots get defaults).
    for v in range(8):
        rec = voices[v] if v < n else None
        for base, spec in VOICE_SPEC.items():
            val = (rec.p.get(base, spec[4]) if rec is not None else spec[4])
            param(f"v{v}_{base}", float(val))

    # Channel strips.
    for i in range(8):
        for key, spec in CH_SPEC.items():
            if not key.startswith(f"ch{i}_"):
                continue
            param(key, float(mixer.get(key, spec[3])))

    # Global FX.
    for key, spec in FX_SPEC.items():
        param(key, float(fx.get(key, spec[3])))

    # VoiceData (modulators + gates per active voice).
    vd = ET.SubElement(state, "VoiceData")
    for v in range(n):
        vt = ET.SubElement(vd, "Voice", idx=str(v))
        modulators_xml(vt, voices[v])
        gate_xml(vt, "Gate", voices[v].gate)
        gate_xml(vt, "FilterGate", voices[v].filtergate)
        gate_xml(vt, "PitchGate", voices[v].pitchgate)

    safe = name.replace("/", "_").replace("\\", "_")
    write_xml(root, os.path.join(PRESETS_DIR, safe + ".muTant"))


# ── Rhythm helpers (32 cells over 2 bars at 1/16) ────────────────────────────
def quarter_pulse(decay=0.6):
    return [env(c, 2, split=0.0, dec=decay) for c in range(0, 32, 4)]

def eighth_pulse(decay=0.4):
    return [env(c, 1, split=0.0, dec=decay) for c in range(0, 32, 2)]

def offbeat():
    return [env(c, 2, split=0.0, dec=0.5) for c in range(2, 32, 4)]

def syncopated():
    starts = [0, 3, 6, 8, 11, 14, 16, 19, 22, 24, 27, 30]
    return [env(c, 1, split=0.0, dec=0.5) for c in starts]

def swell():
    return [env(c, 8, split=0.9, atk=0.3) for c in range(0, 32, 8)]

def half_pulse():
    return [env(c, 4, split=0.0, dec=0.7) for c in range(0, 32, 8)]


# ═════════════════════════════════════════════════════════════════════════════
#  VOICE LIBRARY — 100 single-voice timbres across categories
# ═════════════════════════════════════════════════════════════════════════════
voices = []

def V(name, cat):
    v = Voice(name, cat)
    voices.append(v)
    return v

# A small palette of evocative name parts for variety.
def make(name, cat, **params):
    v = V(name, cat)
    v.set(**params)
    return v

# ── DRONES (sustained, gate bypassed, slow movement) ─────────────────────────
drone_specs = [
    ("Tectonic Hum",   dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=-2, o2_oct=-1, o2_semi=0, flt_type=FT["LP24"], flt_cut=420, flt_res=0.25, o1_lvl=0, o2_lvl=-4, level=-8)),
    ("Glacier Field",  dict(o1_wt=WT["saw2sine"], o2_wt=WT["sine"], o1_oct=-1, o2_oct=-2, o2_semi=7, flt_type=FT["LP24"], flt_cut=900, flt_res=0.2, level=-9)),
    ("Deep Sea Choir", dict(o1_wt=WT["vocal"], o2_wt=WT["formantlo"], o1_oct=-1, o2_oct=-1, o2_semi=12, flt_type=FT["BP12"], flt_cut=700, flt_res=0.35, level=-9)),
    ("Cathedral Air",  dict(o1_wt=WT["organ"], o2_wt=WT["organ"], o1_oct=0, o2_oct=-1, o2_semi=7, flt_type=FT["LP12"], flt_cut=2600, flt_res=0.1, level=-10)),
    ("Iron Lung",      dict(o1_wt=WT["odd"], o2_wt=WT["square"], o1_oct=-2, o2_oct=-2, o2_fine=6, flt_type=FT["LP24"], flt_cut=520, flt_res=0.4, drvChar=INS["TapeSat"], insP1=0.4, level=-9)),
    ("Stone Circle",   dict(o1_wt=WT["hollow"], o2_wt=WT["sine"], o1_oct=-1, o2_oct=-2, o2_semi=5, flt_type=FT["LP12"], flt_cut=1400, flt_res=0.2, level=-9)),
    ("Aurora Bed",     dict(o1_wt=WT["harmstack"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=-1, o2_semi=12, flt_type=FT["LP24"], flt_cut=3200, flt_res=0.15, level=-10)),
    ("Subterranean",   dict(o1_wt=WT["sine"], o2_wt=WT["fm11"], o1_oct=-2, o2_oct=-2, flt_type=FT["LP6"], flt_cut=300, level=-8)),
    ("Monolith",       dict(o1_wt=WT["square"], o2_wt=WT["saw"], o1_oct=-1, o2_oct=-2, o2_fine=-7, flt_type=FT["LP24"], flt_cut=640, flt_res=0.3, level=-10)),
    ("Tidal Drift",    dict(o1_wt=WT["saw2sine"], o2_wt=WT["sq2sine"], o1_oct=-1, o2_oct=0, o2_semi=-5, flt_type=FT["LP12"], flt_cut=1800, flt_res=0.2, level=-10)),
    ("Ghost Engine",   dict(o1_wt=WT["formantsweep"], o2_wt=WT["hollow"], o1_oct=-1, o2_oct=-1, flt_type=FT["BP12"], flt_cut=900, flt_res=0.4, level=-9)),
    ("Magma Chamber",  dict(o1_wt=WT["fm21"], o2_wt=WT["sine"], o1_oct=-2, o2_oct=-2, xmod_index=22, xmod_phaseMode=1, flt_type=FT["LP24"], flt_cut=480, level=-9)),
    ("Frozen Lake",    dict(o1_wt=WT["bell"], o2_wt=WT["sine"], o1_oct=0, o2_oct=-1, o2_semi=12, flt_type=FT["LP12"], flt_cut=4200, flt_res=0.1, level=-11)),
    ("Wind Tunnel",    dict(o1_wt=WT["formanthi"], o2_wt=WT["formantlo"], o1_oct=-1, o2_oct=-1, noise_lvl=-30, flt_type=FT["BP24"], flt_cut=1200, flt_res=0.45, level=-10)),
    ("Black Velvet",   dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=-1, o2_oct=-1, o2_semi=4, flt_type=FT["LP12"], flt_cut=1100, flt_res=0.15, level=-9)),
    ("Reactor Core",   dict(o1_wt=WT["odd"], o2_wt=WT["even"], o1_oct=-1, o2_oct=-2, xmod_depth=30, xmod_ampMode=1, flt_type=FT["LP24"], flt_cut=760, flt_res=0.3, level=-10)),
    ("Slow Nebula",    dict(o1_wt=WT["harmpeak"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=-1, o2_semi=7, flt_type=FT["LP24"], flt_cut=2400, flt_res=0.18, level=-11)),
    ("Abyssal Pad",    dict(o1_wt=WT["hollow"], o2_wt=WT["fm115"], o1_oct=-2, o2_oct=-1, flt_type=FT["LP12"], flt_cut=600, flt_res=0.25, level=-9)),
    ("Temple Bell Bed",dict(o1_wt=WT["bell"], o2_wt=WT["harmstack"], o1_oct=-1, o2_oct=0, o2_semi=12, flt_type=FT["LP24"], flt_cut=3000, flt_res=0.2, level=-11)),
    ("Continental",    dict(o1_wt=WT["saw"], o2_wt=WT["saw"], o1_oct=-2, o2_oct=-1, o2_fine=5, flt_type=FT["LP24"], flt_cut=520, flt_res=0.35, level=-10)),
    ("Dust Veil",      dict(o1_wt=WT["formantlo"], o2_wt=WT["sine"], o1_oct=-1, o2_oct=-2, noise_lvl=-34, flt_type=FT["LP12"], flt_cut=1500, flt_res=0.2, level=-10)),
    ("Drowned Organ",  dict(o1_wt=WT["organ"], o2_wt=WT["hollow"], o1_oct=-1, o2_oct=-1, o2_semi=5, flt_type=FT["LP24"], flt_cut=900, flt_res=0.3, drvChar=INS["TapeSat"], insP1=0.5, level=-10)),
    ("Singing Wire",   dict(o1_wt=WT["odd"], o2_wt=WT["harmpeak"], o1_oct=0, o2_oct=-1, flt_type=FT["BP12"], flt_cut=1300, flt_res=0.45, level=-11)),
    ("Permafrost",     dict(o1_wt=WT["sq2sine"], o2_wt=WT["bell"], o1_oct=-1, o2_oct=0, o2_semi=12, flt_type=FT["LP12"], flt_cut=2800, flt_res=0.15, level=-11)),
    ("Pressure Drop",  dict(o1_wt=WT["sine"], o2_wt=WT["fm21"], o1_oct=-2, o2_oct=-1, xmod_index=18, xmod_phaseMode=1, flt_type=FT["LP24"], flt_cut=600, level=-9)),
]
for nm, pr in drone_specs:
    v = make(nm, "Drones", gate_bypass=1, **pr)
    v.lfo("cs0", "filter.cutoff", round(random.uniform(0.12, 0.30), 2), loopNV="Whole",
          shape=random.choice(["sine", "triangle"]))
    if random.random() < 0.4:
        v.lfo("cs1", "osc1.pos", round(random.uniform(0.1, 0.25), 2), loopNV="Half", shape="sine")

# ── PADS (lush, sustained, brighter, movement on position) ───────────────────
pad_specs = [
    ("Velour Pad",     dict(o1_wt=WT["saw2sine"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=0, o2_fine=7, flt_type=FT["LP12"], flt_cut=3200, flt_res=0.2)),
    ("Crystal Halo",   dict(o1_wt=WT["bell"], o2_wt=WT["harmstack"], o1_oct=0, o2_oct=1, o2_semi=12, flt_type=FT["LP12"], flt_cut=5200, flt_res=0.15)),
    ("Warm Choir",     dict(o1_wt=WT["vocal"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=0, o2_semi=7, flt_type=FT["LP24"], flt_cut=2600, flt_res=0.2)),
    ("Silk Strings",   dict(o1_wt=WT["saw"], o2_wt=WT["saw"], o1_oct=0, o2_oct=0, o2_fine=9, o1_fine=-9, flt_type=FT["LP24"], flt_cut=2800, flt_res=0.25)),
    ("Vapour Trail",   dict(o1_wt=WT["formantsweep"], o2_wt=WT["sine"], o1_oct=0, o2_oct=-1, flt_type=FT["LP12"], flt_cut=3600, flt_res=0.18)),
    ("Aurora Pad",     dict(o1_wt=WT["harmstack"], o2_wt=WT["sq2sine"], o1_oct=0, o2_oct=0, o2_semi=5, flt_type=FT["LP12"], flt_cut=4000, flt_res=0.2)),
    ("Glass Cathedral",dict(o1_wt=WT["organ"], o2_wt=WT["bell"], o1_oct=0, o2_oct=1, flt_type=FT["LP12"], flt_cut=4600, flt_res=0.12)),
    ("Soft Focus",     dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=0, o2_oct=0, o2_semi=4, flt_type=FT["LP12"], flt_cut=2200, flt_res=0.1)),
    ("Northern Light", dict(o1_wt=WT["harmpeak"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=1, o2_semi=7, flt_type=FT["LP24"], flt_cut=3800, flt_res=0.2)),
    ("Breath Pad",     dict(o1_wt=WT["formanthi"], o2_wt=WT["vocal"], o1_oct=0, o2_oct=0, noise_lvl=-36, flt_type=FT["BP12"], flt_cut=2000, flt_res=0.3)),
    ("Pastel Wash",    dict(o1_wt=WT["sq2sine"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=0, o2_fine=-6, flt_type=FT["LP12"], flt_cut=3000, flt_res=0.18)),
    ("Ether Strings",  dict(o1_wt=WT["saw"], o2_wt=WT["sq2sine"], o1_oct=0, o2_oct=1, o2_semi=12, flt_type=FT["LP24"], flt_cut=4400, flt_res=0.22)),
    ("Hymn",           dict(o1_wt=WT["organ"], o2_wt=WT["organ"], o1_oct=0, o2_oct=0, o2_semi=7, flt_type=FT["LP12"], flt_cut=3400, flt_res=0.1)),
    ("Dream Sequence", dict(o1_wt=WT["bell"], o2_wt=WT["harmstack"], o1_oct=0, o2_oct=0, o2_semi=3, flt_type=FT["LP12"], flt_cut=5000, flt_res=0.15)),
    ("Cloud Layer",    dict(o1_wt=WT["saw2sine"], o2_wt=WT["sine"], o1_oct=0, o2_oct=1, flt_type=FT["LP24"], flt_cut=3600, flt_res=0.2)),
    ("Moonstone",      dict(o1_wt=WT["bell"], o2_wt=WT["sq2sine"], o1_oct=0, o2_oct=0, o2_semi=7, flt_type=FT["LP12"], flt_cut=4200, flt_res=0.16)),
    ("Velvet Fog",     dict(o1_wt=WT["hollow"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=-1, flt_type=FT["LP24"], flt_cut=2400, flt_res=0.2)),
    ("Choral Mist",    dict(o1_wt=WT["vocal"], o2_wt=WT["harmstack"], o1_oct=0, o2_oct=0, o2_semi=12, flt_type=FT["LP12"], flt_cut=3600, flt_res=0.18)),
    ("Slow Bloom",     dict(o1_wt=WT["even"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=0, o2_fine=8, flt_type=FT["LP24"], flt_cut=3000, flt_res=0.2)),
    ("Horizon Line",   dict(o1_wt=WT["formanthi"], o2_wt=WT["sine"], o1_oct=0, o2_oct=1, flt_type=FT["LP12"], flt_cut=4800, flt_res=0.14)),
]
for nm, pr in pad_specs:
    v = make(nm, "Pads", gate_bypass=1, level=-8, **pr)
    v.lfo("cs0", "osc1.pos", round(random.uniform(0.15, 0.35), 2), loopNV="Whole", shape="sine")
    if random.random() < 0.5:
        v.lfo("cs1", "filter.cutoff", round(random.uniform(0.1, 0.22), 2), loopNV="Half", shape="triangle")

# ── BASSES (low, focused, bypassed sustain) ──────────────────────────────────
bass_specs = [
    ("Sub Foundation", dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=-2, o2_oct=-2, flt_type=FT["LP24"], flt_cut=380, flt_res=0.2)),
    ("Reese Engine",   dict(o1_wt=WT["saw"], o2_wt=WT["saw"], o1_oct=-1, o2_oct=-1, o2_fine=12, o1_fine=-12, flt_type=FT["LP24"], flt_cut=700, flt_res=0.3)),
    ("Acid Root",      dict(o1_wt=WT["syncsaw"], o2_wt=WT["square"], o1_oct=-1, o2_oct=-2, flt_type=FT["LP24"], flt_cut=600, flt_res=0.6, drvChar=INS["SoftClip"], insP1=0.5)),
    ("Growl Bass",     dict(o1_wt=WT["fm21"], o2_wt=WT["saw"], o1_oct=-1, o2_oct=-2, xmod_index=35, xmod_phaseMode=1, flt_type=FT["LP24"], flt_cut=520, flt_res=0.4)),
    ("Rubber Sub",     dict(o1_wt=WT["sine"], o2_wt=WT["sq2sine"], o1_oct=-2, o2_oct=-1, flt_type=FT["LP12"], flt_cut=440, flt_res=0.25)),
    ("Distorto Bass",  dict(o1_wt=WT["square"], o2_wt=WT["odd"], o1_oct=-1, o2_oct=-1, flt_type=FT["LP24"], flt_cut=900, flt_res=0.3, drvChar=INS["HardClip"], insP1=0.4)),
    ("FM Bottom",      dict(o1_wt=WT["fm31"], o2_wt=WT["sine"], o1_oct=-2, o2_oct=-2, xmod_index=28, flt_type=FT["LP24"], flt_cut=500)),
    ("Tape Sub",       dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=-2, o2_oct=-2, o2_fine=4, flt_type=FT["LP12"], flt_cut=420, drvChar=INS["TapeSat"], insP1=0.5)),
    ("Hollow Bass",    dict(o1_wt=WT["hollow"], o2_wt=WT["sine"], o1_oct=-1, o2_oct=-2, flt_type=FT["LP24"], flt_cut=560, flt_res=0.3)),
    ("Detuned Pulse",  dict(o1_wt=WT["pwm"], o2_wt=WT["pwm"], o1_oct=-1, o2_oct=-1, o2_fine=8, flt_type=FT["LP24"], flt_cut=680, flt_res=0.35)),
    ("Ring Bass",      dict(o1_wt=WT["sine"], o2_wt=WT["fm11"], o1_oct=-1, o2_oct=-1, xmod_depth=45, xmod_ampMode=1, flt_type=FT["LP24"], flt_cut=620)),
    ("Concrete Bass",  dict(o1_wt=WT["saw"], o2_wt=WT["square"], o1_oct=-2, o2_oct=-1, flt_type=FT["LP24"], flt_cut=540, flt_res=0.45, drvChar=INS["Clipper"], insP1=0.4)),
    ("Wobble Sub",     dict(o1_wt=WT["sine"], o2_wt=WT["pwm"], o1_oct=-2, o2_oct=-1, flt_type=FT["LP24"], flt_cut=520, flt_res=0.4)),
    ("Pluck Bass",     dict(o1_wt=WT["fm11"], o2_wt=WT["sine"], o1_oct=-1, o2_oct=-2, xmod_index=20, flt_type=FT["LP24"], flt_cut=800, flt_res=0.3)),
    ("Cavern Bass",    dict(o1_wt=WT["hollow"], o2_wt=WT["triangle"], o1_oct=-2, o2_oct=-2, flt_type=FT["LP12"], flt_cut=400, flt_res=0.2)),
]
for nm, pr in bass_specs:
    make(nm, "Bass", gate_bypass=1, level=-6, o1_lvl=0, o2_lvl=-4, **pr)

# ── LEADS (mid/high, expressive) ─────────────────────────────────────────────
lead_specs = [
    ("Searing Lead",   dict(o1_wt=WT["syncsaw"], o2_wt=WT["saw"], o1_oct=0, o2_oct=0, o2_fine=6, flt_type=FT["LP24"], flt_cut=4000, flt_res=0.3)),
    ("Glass Lead",     dict(o1_wt=WT["bell"], o2_wt=WT["sine"], o1_oct=0, o2_oct=1, flt_type=FT["LP12"], flt_cut=6000, flt_res=0.2)),
    ("FM Spike",       dict(o1_wt=WT["fm31"], o2_wt=WT["fm21"], o1_oct=0, o2_oct=0, xmod_index=48, xmod_phaseMode=1, flt_type=FT["LP12"], flt_cut=5000)),
    ("Vox Lead",       dict(o1_wt=WT["vocal"], o2_wt=WT["formanthi"], o1_oct=0, o2_oct=0, flt_type=FT["BP12"], flt_cut=2400, flt_res=0.35)),
    ("Square Solo",    dict(o1_wt=WT["square"], o2_wt=WT["pwm"], o1_oct=0, o2_oct=0, o2_fine=-5, flt_type=FT["LP24"], flt_cut=3600, flt_res=0.25)),
    ("Harmonic Lead",  dict(o1_wt=WT["harmpeak"], o2_wt=WT["odd"], o1_oct=0, o2_oct=0, flt_type=FT["LP12"], flt_cut=4400, flt_res=0.2)),
    ("Sync Scream",    dict(o1_wt=WT["syncsaw"], o2_wt=WT["syncsaw"], o1_oct=0, o2_oct=0, sync=1, flt_type=FT["LP24"], flt_cut=4800, flt_res=0.3)),
    ("Whistle",        dict(o1_wt=WT["sine"], o2_wt=WT["sine"], o1_oct=1, o2_oct=1, o2_fine=4, flt_type=FT["LP12"], flt_cut=7000, flt_res=0.1)),
    ("Resin Lead",     dict(o1_wt=WT["saw"], o2_wt=WT["odd"], o1_oct=0, o2_oct=0, o2_fine=-6, flt_type=FT["LP24"], flt_cut=3800, flt_res=0.35)),
    ("Theremin",       dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=1, o2_oct=0, o1_penv_depth=2, flt_type=FT["LP12"], flt_cut=5200, flt_res=0.12)),
]
for nm, pr in lead_specs:
    v = make(nm, "Leads", gate_bypass=1, level=-8, **pr)
    v.lfo("cs0", "osc1.fine", round(random.uniform(0.05, 0.12), 2), loopNV="Quarter", shape="sine")

# ── TEXTURES / FX (xmod-heavy, noise, inharmonic) ────────────────────────────
texture_specs = [
    ("Radio Static",   dict(o1_wt=WT["sparse"], o2_wt=WT["fm31"], noise_lvl=-18, xmod_depth=60, xmod_ampMode=1, flt_type=FT["BP12"], flt_cut=1800, flt_res=0.4)),
    ("Metal Shards",   dict(o1_wt=WT["bell"], o2_wt=WT["fm21"], xmod_depth=70, xmod_ampMode=1, flt_type=FT["BP24"], flt_cut=2600, flt_res=0.5)),
    ("Insect Swarm",   dict(o1_wt=WT["fm115"], o2_wt=WT["odd"], xmod_index=55, xmod_phaseMode=2, flt_type=FT["BP12"], flt_cut=2200, flt_res=0.4)),
    ("Frequency Shift",dict(o1_wt=WT["saw"], o2_wt=WT["sine"], xmod_ampMode=2, xmod_ssb=600, flt_type=FT["LP12"], flt_cut=3000)),
    ("Broken Machine", dict(o1_wt=WT["square"], o2_wt=WT["sparse"], xmod_index=80, xmod_fdbk=1, flt_type=FT["CombPlus"], flt_cut=1200, flt_res=0.5, drvChar=INS["Bitcrusher"], insP1=0.5)),
    ("Alien Voice",    dict(o1_wt=WT["vocal"], o2_wt=WT["formantsweep"], xmod_ampMode=2, xmod_ssb=-400, flt_type=FT["BP12"], flt_cut=1600, flt_res=0.4)),
    ("Granular Cloud", dict(o1_wt=WT["sparse"], o2_wt=WT["harmstack"], xmod_index=40, flt_type=FT["BP24"], flt_cut=2400, flt_res=0.45)),
    ("Comb Resonator", dict(o1_wt=WT["saw"], o2_wt=WT["square"], flt_type=FT["CombMinus"], flt_cut=800, flt_res=0.6)),
    ("Ring World",     dict(o1_wt=WT["sine"], o2_wt=WT["fm21"], xmod_depth=90, xmod_ampMode=1, flt_type=FT["LP12"], flt_cut=3200)),
    ("Data Corruption",dict(o1_wt=WT["odd"], o2_wt=WT["sparse"], xmod_index=65, xmod_phaseMode=2, drvChar=INS["Bitcrusher"], insP1=0.7, flt_type=FT["HP12"], flt_cut=400)),
    ("Solar Wind",     dict(o1_wt=WT["formantsweep"], o2_wt=WT["harmstack"], xmod_ampMode=2, xmod_ssb=250, flt_type=FT["BP12"], flt_cut=2000, flt_res=0.4)),
    ("Tape Ghosts",    dict(o1_wt=WT["vocal"], o2_wt=WT["sparse"], xmod_index=35, xmod_fdbk=1, drvChar=INS["TapeSat"], insP1=0.6, flt_type=FT["BP24"], flt_cut=1800, flt_res=0.4)),
]
for nm, pr in texture_specs:
    v = make(nm, "Textures", gate_bypass=1, level=-10, **pr)
    v.lfo("cs0", "xmod.index", round(random.uniform(0.2, 0.45), 2), loopNV="Half",
          shape=random.choice(["sine", "triangle", "random"]))

# ── RHYTHMIC / GATED (gate envelopes; audible while playing) ──────────────────
rhythm_specs = [
    ("Pulse Engine",   quarter_pulse(), dict(o1_wt=WT["saw"], o2_wt=WT["square"], o1_oct=-1, o2_oct=-1, flt_type=FT["LP24"], flt_cut=1600, flt_res=0.4)),
    ("Ticker",         eighth_pulse(),  dict(o1_wt=WT["pwm"], o2_wt=WT["sine"], o1_oct=0, o2_oct=-1, flt_type=FT["BP12"], flt_cut=2400, flt_res=0.3)),
    ("Offbeat Stab",   offbeat(),       dict(o1_wt=WT["square"], o2_wt=WT["saw"], o1_oct=0, o2_oct=0, flt_type=FT["LP24"], flt_cut=2000, flt_res=0.45)),
    ("Sync Sequence",  syncopated(),    dict(o1_wt=WT["syncsaw"], o2_wt=WT["pwm"], o1_oct=-1, o2_oct=0, flt_type=FT["LP24"], flt_cut=2200, flt_res=0.4)),
    ("Slow Swell",     swell(),         dict(o1_wt=WT["saw2sine"], o2_wt=WT["bell"], o1_oct=0, o2_oct=0, flt_type=FT["LP12"], flt_cut=3000, flt_res=0.2)),
    ("Half Note Pump", half_pulse(),    dict(o1_wt=WT["organ"], o2_wt=WT["saw2sine"], o1_oct=0, o2_oct=-1, flt_type=FT["LP12"], flt_cut=2600, flt_res=0.2)),
    ("Plucked Drone",  quarter_pulse(0.8), dict(o1_wt=WT["bell"], o2_wt=WT["sine"], o1_oct=0, o2_oct=0, flt_type=FT["LP12"], flt_cut=4000, flt_res=0.2)),
    ("Stutter Bass",   eighth_pulse(0.6),  dict(o1_wt=WT["saw"], o2_wt=WT["square"], o1_oct=-2, o2_oct=-1, flt_type=FT["LP24"], flt_cut=900, flt_res=0.4)),
    ("Tremolo Pad",    eighth_pulse(0.2),  dict(o1_wt=WT["saw2sine"], o2_wt=WT["sq2sine"], o1_oct=0, o2_oct=0, flt_type=FT["LP12"], flt_cut=3200, flt_res=0.2)),
    ("Gated Choir",    quarter_pulse(0.5), dict(o1_wt=WT["vocal"], o2_wt=WT["formantlo"], o1_oct=0, o2_oct=0, flt_type=FT["BP12"], flt_cut=1800, flt_res=0.35)),
    ("Machine Pulse",  syncopated(),    dict(o1_wt=WT["fm21"], o2_wt=WT["square"], o1_oct=-1, o2_oct=-1, xmod_index=30, flt_type=FT["LP24"], flt_cut=1400, flt_res=0.4)),
    ("Heartbeat",      half_pulse(),    dict(o1_wt=WT["sine"], o2_wt=WT["triangle"], o1_oct=-2, o2_oct=-2, flt_type=FT["LP12"], flt_cut=500)),
    ("Arpeggio Bed",   eighth_pulse(0.5),  dict(o1_wt=WT["harmstack"], o2_wt=WT["bell"], o1_oct=0, o2_oct=1, flt_type=FT["LP12"], flt_cut=4200, flt_res=0.18)),
    ("Throb",          quarter_pulse(0.4), dict(o1_wt=WT["odd"], o2_wt=WT["saw"], o1_oct=-1, o2_oct=-1, flt_type=FT["LP24"], flt_cut=1200, flt_res=0.45)),
    ("Ratchet",        eighth_pulse(0.7),  dict(o1_wt=WT["sparse"], o2_wt=WT["fm31"], o1_oct=0, o2_oct=0, xmod_index=40, flt_type=FT["BP12"], flt_cut=2600, flt_res=0.4)),
    ("Morse Code",     syncopated(),    dict(o1_wt=WT["sine"], o2_wt=WT["square"], o1_oct=0, o2_oct=-1, flt_type=FT["LP12"], flt_cut=2000)),
    ("Iron Pendulum",  half_pulse(),    dict(o1_wt=WT["odd"], o2_wt=WT["hollow"], o1_oct=-1, o2_oct=-2, flt_type=FT["LP24"], flt_cut=800, flt_res=0.4, drvChar=INS["SoftClip"], insP1=0.4)),
    ("Chime Sequence", quarter_pulse(0.7), dict(o1_wt=WT["bell"], o2_wt=WT["harmstack"], o1_oct=0, o2_oct=1, flt_type=FT["LP12"], flt_cut=5000, flt_res=0.15)),
]
for nm, g, pr in rhythm_specs:
    v = make(nm, "Rhythmic", gate_bypass=0, gate_gap=8, level=-7, **pr)
    v.add_gate(g, "gate")
    if random.random() < 0.4:
        v.lfo("cs0", "filter.cutoff", round(random.uniform(0.15, 0.3), 2), loopNV="Whole", shape="triangle")

assert len(voices) == 100, f"expected 100 voices, got {len(voices)}"


# ═════════════════════════════════════════════════════════════════════════════
#  FULL PRESETS — 100 combinations of a few voices + mixer + FX
# ═════════════════════════════════════════════════════════════════════════════
by_cat = {}
for v in voices:
    by_cat.setdefault(v.category, []).append(v)

ROOTS = {"C":0,"C#":1,"D":2,"D#":3,"E":4,"F":5,"F#":6,"G":7,"G#":8,"A":9,"A#":10,"B":11}

# FX preset palettes — musical (delay + reverb + an effect), varied per template.
def fx_lush():
    return dict(rev_en=1, rev_algo=1, rev_size=0.85, rev_lvl=1.0, rev_damp=0.35,
                dly_en=1, dly_fb=0.35, dly_syncDenom=2, eff_en=1, eff_algo=0, eff_p0=0.4)
def fx_space():
    return dict(rev_en=1, rev_algo=1, rev_size=0.95, rev_pre=40, rev_lvl=1.0, rev_diff=0.9,
                dly_en=1, dly_fb=0.5, dly_count=2, dly_syncDenom=1, eff_en=1, eff_algo=2)
def fx_plate():
    return dict(rev_en=1, rev_algo=2, rev_size=0.6, rev_lvl=0.9, rev_damp=0.25,
                dly_en=1, dly_fb=0.25, eff_en=1, eff_algo=1, eff_p0=0.3)
def fx_dub():
    return dict(rev_en=1, rev_algo=1, rev_size=0.8, dly_en=1, dly_fb=0.6, dly_count=3,
                dly_spread=0.6, dly_dirt=0.3, eff_en=1, eff_algo=3)
def fx_dry():
    return dict(rev_en=1, rev_algo=0, rev_size=0.4, rev_lvl=0.5, dly_en=1, dly_fb=0.2,
                eff_en=1, eff_algo=0, eff_p0=0.25)
FX_PALETTE = [fx_lush, fx_space, fx_plate, fx_dub, fx_dry]

# Curated full-preset templates: (name, category, [voice categories to draw from], n voices, fx, sends)
full_templates = [
    ("Cavern",          "Soundscapes", ["Drones","Pads"],            3, fx_space, (0.5, 0.3)),
    ("Slow Tide",       "Soundscapes", ["Drones","Pads","Textures"], 4, fx_lush,  (0.4, 0.3)),
    ("Event Horizon",   "Soundscapes", ["Drones","Textures"],        3, fx_space, (0.6, 0.4)),
    ("Glass Garden",    "Soundscapes", ["Pads","Leads"],             3, fx_plate, (0.3, 0.4)),
    ("Deep Current",    "Soundscapes", ["Drones","Bass"],            3, fx_lush,  (0.3, 0.2)),
    ("Northern Sky",    "Ambient",     ["Pads","Drones"],            4, fx_lush,  (0.5, 0.3)),
    ("Tape Memory",     "Ambient",     ["Pads","Textures"],          3, fx_dub,   (0.4, 0.5)),
    ("Suspended",       "Ambient",     ["Drones","Pads","Leads"],    4, fx_space, (0.5, 0.4)),
    ("Quiet Storm",     "Ambient",     ["Pads","Drones"],            3, fx_plate, (0.4, 0.3)),
    ("Lighthouse",      "Ambient",     ["Pads","Bass"],              3, fx_lush,  (0.3, 0.3)),
    ("Engine Room",     "Rhythmic",    ["Rhythmic","Bass"],          3, fx_plate, (0.2, 0.3)),
    ("Pulse Grid",      "Rhythmic",    ["Rhythmic","Drones"],        4, fx_dub,   (0.3, 0.4)),
    ("Mechanism",       "Rhythmic",    ["Rhythmic","Textures"],      3, fx_dry,   (0.2, 0.3)),
    ("Night Drive",     "Rhythmic",    ["Rhythmic","Bass","Pads"],   4, fx_plate, (0.3, 0.3)),
    ("Heartland",       "Rhythmic",    ["Rhythmic","Pads"],          3, fx_lush,  (0.3, 0.4)),
    ("Underworld",      "Bass & Low",  ["Bass","Drones"],            3, fx_dry,   (0.2, 0.2)),
    ("Foundation",      "Bass & Low",  ["Bass","Pads"],              3, fx_plate, (0.2, 0.3)),
    ("Sub Cathedral",   "Bass & Low",  ["Bass","Drones","Pads"],     4, fx_space, (0.3, 0.3)),
    ("Acid Cellar",     "Bass & Low",  ["Bass","Rhythmic"],          3, fx_dub,   (0.3, 0.3)),
    ("Lead Story",      "Melodic",     ["Leads","Pads","Bass"],      4, fx_plate, (0.3, 0.4)),
]

os.makedirs(VOICES_DIR, exist_ok=True)
os.makedirs(PRESETS_DIR, exist_ok=True)

full_count = 0
for ti, (base_name, cat, draw_cats, nv, fxfn, sends) in enumerate(full_templates):
    for variant in range(5):    # 20 templates × 5 = 100
        name = base_name if variant == 0 else f"{base_name} {variant + 1}"
        # Pick nv voices from the requested categories (cycle through cats).
        pool = []
        for c in draw_cats:
            pool += by_cat.get(c, [])
        chosen = random.sample(pool, min(nv, len(pool)))
        nv_actual = len(chosen)

        # Mixer: spread pans, set per-channel level + FX sends so the rack is heard.
        mixer = {"root": random.choice(list(ROOTS.values())), "scale": 0, "mstrLoop": 0}
        sendDly, sendRev = sends
        for ci, _ in enumerate(chosen):
            pan = round(((ci / max(1, nv_actual - 1)) - 0.5) * 1.2, 2) if nv_actual > 1 else 0.0
            mixer[f"ch{ci}_lvl"]  = round(random.uniform(0.7, 0.92), 3)
            mixer[f"ch{ci}_pan"]  = max(-1.0, min(1.0, pan))
            mixer[f"ch{ci}_sendRev"] = round(sendRev * random.uniform(0.7, 1.1), 3)
            mixer[f"ch{ci}_sendDly"] = round(sendDly * random.uniform(0.5, 1.0), 3)

        fx = fxfn()
        write_full_preset(name, f"{cat} — {', '.join(v.name for v in chosen)}",
                          cat, chosen, mixer, fx)
        full_count += 1

# ── Write all voice presets ──────────────────────────────────────────────────
for v in voices:
    write_voice_preset(v)

print(f"Wrote {len(voices)} voice presets  -> {VOICES_DIR}")
print(f"Wrote {full_count} full presets   -> {PRESETS_DIR}")
