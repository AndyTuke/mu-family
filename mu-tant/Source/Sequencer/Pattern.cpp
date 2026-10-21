#include "Sequencer/Pattern.h"
#include "UI/PatternHost.h"                  // the layer's host: the wavetable bank a saved path resolves into
#include "Modulation/MuTantModDest.h"        // the modulation target table
#include "Modulation/ModulatorSerialise.h"   // mu-core: shared modulator (de)serialise
#include "Sequencer/GatePatternSerialise.h"  // the gate (de)serialise
#include "Sequencer/BeatWrap.h"              // mu-core: wrapBeat
#include "Persistence/PatternParams.h"       // the v{N}_ ids
#include "Modulation/LaneModulation.h"       // mu-core: mu_mod::resolveLane
#include "Audio/SpinLock.h"                  // mu-core: the envelopes' spin locks
#include "Audio/TantScales.h"                    // kBaseOctave

#include <algorithm>
#include <cmath>
#include <limits>

// The Pattern layer: its persistence (its node's non-parameter data), its stepped-pitch flags and its half
// of the render contract.
namespace mu_tant
{

namespace
{
    // Drops assignments to destinations this layer type does not expose (source ids are ControlSequence
    // ids, left unchecked).
    bool isValidModDest(const std::string& id)
    {
        for (int i = 0; i < kModDestCount; ++i)
            if (id == kModDestTable[i].id) return true;
        return false;
    }

    // True if a Stepped ControlSequence of `layer` is assigned to `destId`: those snap pitch to semitones
    // (melodies); smooth sources and the envelope glide. Compares "csN_output" against each sequence id
    // without building strings.
    bool hasSteppedSource(const Layer& layer, const char* destId)
    {
        for (const auto& a : layer.modulationMatrix.getAssignments())
        {
            if (a.destinationId != destId) continue;
            for (const auto& cs : layer.controlSequences)
            {
                if (cs.mode != ControlSequence::Mode::Stepped) continue;
                const auto& sid = cs.id;
                if (a.sourceId.size() == sid.size() + 7
                    && a.sourceId.compare(0, sid.size(), sid) == 0
                    && a.sourceId.compare(sid.size(), 7, "_output") == 0)
                    return true;
            }
        }
        return false;
    }
}

// The voice's non-parameter data onto its node: the user wavetable paths, the modulators and the three
// envelope patterns, in this order (saved files compare byte for byte).
void Pattern::writeExtras(juce::ValueTree& node) const
{
    if (osc1UserPath.isNotEmpty()) node.setProperty("o1WtPath", osc1UserPath, nullptr);
    if (osc2UserPath.isNotEmpty()) node.setProperty("o2WtPath", osc2UserPath, nullptr);
    node.appendChild(mu_pp::serialiseModulators(*this),          nullptr);
    node.appendChild(serialiseGate(gate),                         nullptr);
    node.appendChild(serialiseGate(filterGate, "FilterGate"),     nullptr);
    node.appendChild(serialiseGate(pitchGate,  "PitchGate"),      nullptr);
}

// The voice's non-parameter data from its node. Absent children clear (an invalid tree empties a gate / the
// modulators); per-pattern spin locks and modLock guard the audio thread, so no layersLock is needed.
void Pattern::applyExtras(const juce::ValueTree& node)
{
    mu_pp::clearModulators(*this);
    mu_pp::deserialiseModulators(node.getChildWithName("Modulators"), *this, {}, isValidModDest);
    refreshPitchQuantFlags();   // the assignments changed

    const int maxCells = std::numeric_limits<int>::max();
    deserialiseGate(node.getChildWithName("Gate"),       gate,       maxCells);
    deserialiseGate(node.getChildWithName("FilterGate"), filterGate, maxCells);
    deserialiseGate(node.getChildWithName("PitchGate"),  pitchGate,  maxCells);

    // A missing file keeps its path (the UI shows "missing") but resolves to -1, so the oscillator falls back
    // to its factory selection.
    auto resolve = [this](const juce::String& path, juce::String& storedPath, std::atomic<int>& index)
    {
        storedPath = path;
        index.store(path.isNotEmpty() && host.ptr != nullptr ? host.ptr->resolvePatternWavetable(path) : -1);
    };
    resolve(node.getProperty("o1WtPath").toString(), osc1UserPath, osc1UserIndex);
    resolve(node.getProperty("o2WtPath").toString(), osc2UserPath, osc2UserIndex);
}

void Pattern::refreshPitchQuantFlags()
{
    osc1SemiStepped.store(hasSteppedSource(*this, "osc1.semi"), std::memory_order_relaxed);
    osc2SemiStepped.store(hasSteppedSource(*this, "osc2.semi"), std::memory_order_relaxed);
}

// ── The render contract ───────────────────────────────────────────────────────
// The per-block chain that used to be mu-Tant's processor glue (readConfig, applyModulation,
// applyFilterEnvelope, applyPitchEnvelope, renderVoice, renderRetiringVoice), now the layer's own.
// docs/mu-tronic/design.md 2.1.

namespace
{
    // Filter-cutoff proportion-space helpers — same skew as mu-clid (cutoff range 20..20000 Hz, skewForCentre
    // 640 Hz → skewFactor ≈ 0.2). The filter envelope adds in proportion space so a given depth always sweeps
    // the same visual arc of the knob regardless of the base cutoff.
    inline float tantPropFromCutoff(float hz)
    {
        return std::pow(juce::jlimit(0.0f, 1.0f, (hz - 20.0f) / 19980.0f), 0.2f);
    }
    inline float tantCutoffFromProp(float p)
    {
        return 20.0f + 19980.0f * std::pow(juce::jlimit(0.0f, 1.0f, p), 5.0f);
    }

    // Index of each resolved value in the resolveLane `out` array (kModDestTable order).
    enum { D_o1Oct = 0, D_o1Semi, D_o1Fine, D_o1Pos, D_o1Penv,
           D_o2Oct, D_o2Semi, D_o2Fine, D_o2Pos, D_o2Penv,
           D_xmIndex, D_xmDepth, D_xmSsb, D_o1Lvl, D_o2Lvl, D_nzLvl,
           D_fCut, D_fRes, D_fDrv, D_fLoCut, D_fEnv,
           D_f2Cut, D_f2Res, D_f2Drv, D_f2LoCut, D_f2Env, D_level,
           D_insP1, D_insP2, D_insP3, D_insP4 };

    // Compile-time guard: pin the D_* indices to the table rows, so an insert / reorder that would read the
    // wrong resolved value for a target fails to COMPILE.
    constexpr bool cstrEq(const char* a, const char* b)
    {
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
    static_assert(cstrEq(kModDestTable[D_o1Semi].id, "osc1.semi"),     "D_o1Semi desync");
    static_assert(cstrEq(kModDestTable[D_fCut].id,   "filter.cutoff"), "D_fCut desync");
    static_assert(cstrEq(kModDestTable[D_level].id,  "level"),         "D_level desync");
    static_assert(cstrEq(kModDestTable[D_insP4].id,  "insert.p4"),     "D_insP4 desync");
    static_assert(D_insP4 + 1 == Pattern::kNumModDests, "every target has an index");

    constexpr float kNoteGateFadeSecs = 0.008f;   // Note mode's anti-click ramp (8 ms)

    // The wavetable bank's try-lock for one call (audio thread): never waits. No lock bound (a host without an
    // appendable bank) counts as held.
    struct BankTryLock
    {
        explicit BankTryLock(const juce::CriticalSection* l) noexcept : lock(l), held(l == nullptr || l->tryEnter()) {}
        ~BankTryLock() { if (lock != nullptr && held) lock->exit(); }
        bool isLocked() const noexcept { return held; }
        BankTryLock(const BankTryLock&) = delete;
        BankTryLock& operator=(const BankTryLock&) = delete;
        const juce::CriticalSection* lock;
        bool held;
    };
}

void Pattern::prepare(double sampleRate, int blockSize)
{
    insert->prepare(sampleRate, blockSize);
    tailScratch.setSize(2, juce::jmax(1, blockSize), false, true, false);
    haveResolved = false;   // a prepare is a fresh start: no held modulated values (C12)
}

void Pattern::bindParams(juce::AudioProcessorValueTreeState& apvts, int slot)
{
    auto B = [&](const char* suffix) { return mu_core::ParamBinding(apvts.getRawParameterValue(layerParamId(slot, suffix)), this); };
    auto& r = rows;
    r.o1Oct = B("o1_oct");  r.o1Semi = B("o1_semi"); r.o1Fine = B("o1_fine"); r.o1Pos = B("o1_pos"); r.o1Wt = B("o1_wt");
    r.o2Oct = B("o2_oct");  r.o2Semi = B("o2_semi"); r.o2Fine = B("o2_fine"); r.o2Pos = B("o2_pos"); r.o2Wt = B("o2_wt");
    r.xmodPhaseMode = B("xmod_phaseMode"); r.xmodIndex = B("xmod_index"); r.sync = B("sync");
    r.xmodFdbk = B("xmod_fdbk"); r.xmodAmpMode = B("xmod_ampMode"); r.xmodDepth = B("xmod_depth"); r.xmodSsb = B("xmod_ssb");
    r.o1Lvl = B("o1_lvl"); r.o2Lvl = B("o2_lvl"); r.noiseLvl = B("noise_lvl"); r.noiseType = B("noise_type");
    r.fltType = B("flt_type"); r.fltCut = B("flt_cut"); r.fltRes = B("flt_res"); r.fltEnvDepth = B("flt_env_depth");
    r.fltDrv = B("flt_drv"); r.fltLoCut = B("flt_lo_cut");
    r.flt2Type = B("flt2_type"); r.flt2Cut = B("flt2_cut"); r.flt2Res = B("flt2_res"); r.flt2EnvDepth = B("flt2_env_depth");
    r.flt2Drv = B("flt2_drv"); r.flt2LoCut = B("flt2_lo_cut"); r.fltSeries = B("flt_series");
    r.o1PenvDepth = B("o1_penv_depth"); r.o2PenvDepth = B("o2_penv_depth");
    r.level = B("level"); r.gateGap = B("gate_gap"); r.gateBypass = B("gate_bypass");
    r.drvChar = B("drvChar"); r.insP1 = B("insP1"); r.insP2 = B("insP2"); r.insP3 = B("insP3"); r.insP4 = B("insP4");
    r.root  = mu_core::ParamBinding(apvts.getRawParameterValue(kRootParamId), this);
    r.scale = mu_core::ParamBinding(apvts.getRawParameterValue(kScaleParamId), this);
    jassert(r.o1Oct.isBound() && r.insP4.isBound() && r.root.isBound() && r.scale.isBound());   // an id drift from the layout (debug)

    // The modulation inputs: the ranges are the parameters' own, so the cutoff's proportion seed matches its skewed knob.
    modMap.reserve((size_t) kNumModDests + 4);   // pre-sized: no audio-thread rehash
    for (int i = 0; i < kNumModDests; ++i)
    {
        const auto id = layerParamId(slot, kModDestTable[i].param);
        modIds[(size_t) i]    = kModDestTable[i].id;
        modRanges[(size_t) i] = apvts.getParameterRange(id);
        modAtoms[(size_t) i]  = apvts.getRawParameterValue(id);
        jassert(modAtoms[(size_t) i] != nullptr);   // a target whose parameter drifted from the layout
        modMap.emplace(std::string_view(kModDestTable[i].id), 0.0f);   // pre-insert key
    }

    if (host.ptr != nullptr)
    {
        engine   = host.ptr->getPatternEngine(slot);
        bankLock = host.ptr->getPatternBankLock();
    }
    haveResolved = false;   // values held for C12 belong to the slot bound before
}

VoiceConfig Pattern::readConfig() const noexcept
{
    const auto& p = rows;
    VoiceConfig c;
    // Tonal centre is global.
    c.root         = (int) p.root.load();
    c.scaleIndex   = (int) p.scale.load();
    // Per-voice osc pitch (integer) + position.
    c.osc1Octave   = (int) p.o1Oct.load();
    c.osc1Semi     = (int) p.o1Semi.load();
    c.osc1Fine     = (int) p.o1Fine.load();
    c.osc1Pos      = p.o1Pos.load();
    c.osc2Octave   = (int) p.o2Oct.load();
    c.osc2Semi     = (int) p.o2Semi.load();
    c.osc2Fine     = (int) p.o2Fine.load();
    c.osc2Pos      = p.o2Pos.load();
    // Wavetable: a loaded user table (resolved bank index) overrides the factory o{1,2}_wt selection.
    {
        const int u1 = osc1UserIndex.load();
        const int u2 = osc2UserIndex.load();
        c.osc1Wavetable = (u1 >= 0) ? u1 : (int) p.o1Wt.load();
        c.osc2Wavetable = (u2 >= 0) ? u2 : (int) p.o2Wt.load();
    }
    // X-Mod — Lane A (phase/index) + Lane B (amplitude/multiply).
    c.xmodPhaseMode = (int) p.xmodPhaseMode.load();   // 0 FM, 1 PM, 2 TZFM
    c.xmodIndex     = p.xmodIndex.load() * 0.01f;     // 0..100 → 0..1
    c.sync          = p.sync.load() > 0.5f;
    c.xmodFeedback  = p.xmodFdbk.load() > 0.5f;
    c.xmodAmpMode   = (int) p.xmodAmpMode.load();     // 0 AM, 1 RM, 2 SSB
    c.xmodDepth     = p.xmodDepth.load() * 0.01f;     // -100..100 → -1..1
    c.xmodSsbHz     = p.xmodSsb.load();               // Hz
    // Per-source levels.
    c.osc1LevelDb  = p.o1Lvl.load();
    c.osc2LevelDb  = p.o2Lvl.load();
    c.noiseLevelDb = p.noiseLvl.load();
    c.noiseType    = (int) p.noiseType.load();
    c.filterType      = (int) p.fltType.load();
    c.filterCutoff    = p.fltCut.load();
    c.filterRes       = p.fltRes.load();
    c.filterEnvDepth  = p.fltEnvDepth.load();
    c.filterDrive     = p.fltDrv.load();
    c.filterLowCutHz  = p.fltLoCut.load();
    c.filter2Type     = (int) p.flt2Type.load();
    c.filter2Cutoff   = p.flt2Cut.load();
    c.filter2Res      = p.flt2Res.load();
    c.filter2EnvDepth = p.flt2EnvDepth.load();
    c.filter2Drive    = p.flt2Drv.load();
    c.filter2LowCutHz = p.flt2LoCut.load();
    c.filterSeries    = p.fltSeries.load() > 0.5f;
    c.osc1PitchEnvDepth = p.o1PenvDepth.load();
    c.osc2PitchEnvDepth = p.o2PenvDepth.load();
    c.levelDb         = p.level.load();
    return c;
}

// The keys of this call into the held stack (last-note priority). A Free↔Note switch resets the stack and
// the own-output gate, so neither mode inherits stale state. Every call, live or parked, before anything that
// can skip the render: every Pattern hears the same keys, so a layer that goes live knows what is held.
int Pattern::hearNotes(const mu_core::RenderContext& ctx) noexcept
{
    const int mode = noteMode.load(std::memory_order_relaxed);
    if (mode != notes.lastMode)
    {
        notes.held.clear();
        // A layer that has not rendered yet starts with its gate settled for the mode; a live one keeps its gain on
        // Free to Note, so the own-output ramp closes it over 8 ms instead of snapping a sounding drone to silence.
        if (mode != 1 || ! notes.seeded) notes.gateGain = (mode == 1) ? 0.0f : 1.0f;
        notes.lastMode = mode;
    }
    notes.seeded = true;
    if (ctx.midi.inputReset) notes.held.clear();   // MIDI In changed: a release on the old channel will not arrive
    if (mode != 1) return mode;

    // A note-on (velocity above 0) pushes, a note-off or a note-on at velocity 0 removes (juce::MidiMessage's
    // isNoteOn / isNoteOff), on every channel.
    mu_core::forEachMidi(ctx, [this](const mu_core::MidiEvent& e, int)
    {
        if (e.size < 1) return;
        const int type = e.bytes[0] & 0xf0;
        if (type == 0x90 && e.bytes[2] != 0)       notes.held.push(e.bytes[1]);
        else if (type == 0x80 || type == 0x90)     notes.held.remove(e.bytes[1]);
    });
    return mode;
}

void Pattern::applyModulation(double beat, VoiceConfig& cfg, std::array<float, 4>& insP) noexcept
{
    // Resolve this voice's modulation through the shared mu-core range-based routing: seed each destination's
    // proportion from its APVTS atom, run the voice's matrix under a try-lock (skipped on contention → the
    // voice plays un-modulated this block), then write the modulated values back in param units.
    float out[kNumModDests];
    const bool resolved = mu_mod::resolveLane(this, beat, kNumModDests, modIds.data(), modAtoms.data(),
                                              modRanges.data(), modMap, out);

    // A block that misses the matrix lock (the message thread is editing this voice's modulators) holds the last
    // resolved values, so no target snaps back to its unmodulated knob for a block (C12). Before the first resolve
    // of this slot (a fresh bind, a prepare, a move, a voice that has just gone live) it takes the unmodulated
    // values, as every block of a miss did before.
    if (resolved || ! haveResolved)
    {
        std::copy(out, out + kNumModDests, lastResolved.begin());
        haveResolved = haveResolved || resolved;
    }
    else
        std::copy(lastResolved.begin(), lastResolved.end(), out);

    // Octave/fine pitch dests round to the nearest step (the proportion round-trip isn't bit-exact, so a plain
    // truncation could drop a step); the rest map straight across, already range-clamped by resolveLane.
    cfg.osc1Octave   = juce::roundToInt(out[D_o1Oct]);
    cfg.osc1Fine     = juce::roundToInt(out[D_o1Fine]);
    cfg.osc1Pos      = out[D_o1Pos];
    cfg.osc2Octave   = juce::roundToInt(out[D_o2Oct]);
    cfg.osc2Fine     = juce::roundToInt(out[D_o2Fine]);
    cfg.osc2Pos      = out[D_o2Pos];

    // Pitch (scale degrees): a Stepped CS snaps the whole result to a degree (melodies); a smooth
    // source glides through the scale via a fractional degree offset (ToneMod) on top of the exact
    // integer base. The offset is in degrees, not semitones, so a smooth source reaches the same
    // notes as a stepped one; the pitch envelope adds chromatic semitones separately (SemiMod).
    {
        const float o1Base = (float) rows.o1Semi.load();
        const float o2Base = (float) rows.o2Semi.load();
        const bool  o1Stepped = osc1SemiStepped.load(std::memory_order_relaxed);
        const bool  o2Stepped = osc2SemiStepped.load(std::memory_order_relaxed);
        cfg.osc1SemiMod = cfg.osc2SemiMod = 0.0f;   // the pitch envelope adds to these below
        if (o1Stepped) { cfg.osc1Semi = juce::roundToInt(out[D_o1Semi]); cfg.osc1ToneMod = 0.0f; }
        else           { cfg.osc1Semi = juce::roundToInt(o1Base);        cfg.osc1ToneMod = out[D_o1Semi] - o1Base; }
        if (o2Stepped) { cfg.osc2Semi = juce::roundToInt(out[D_o2Semi]); cfg.osc2ToneMod = 0.0f; }
        else           { cfg.osc2Semi = juce::roundToInt(o2Base);        cfg.osc2ToneMod = out[D_o2Semi] - o2Base; }
    }
    cfg.xmodIndex    = out[D_xmIndex] * 0.01f;   // param 0..100 → engine 0..1
    cfg.xmodDepth    = out[D_xmDepth] * 0.01f;   // param -100..100 → engine -1..1
    cfg.xmodSsbHz    = out[D_xmSsb];             // Hz (direct)
    cfg.osc1LevelDb  = out[D_o1Lvl];
    cfg.osc2LevelDb  = out[D_o2Lvl];
    cfg.noiseLevelDb = out[D_nzLvl];
    cfg.osc1PitchEnvDepth = out[D_o1Penv];
    cfg.osc2PitchEnvDepth = out[D_o2Penv];
    cfg.filterCutoff   = out[D_fCut];
    cfg.filterRes      = out[D_fRes];
    cfg.filterDrive    = out[D_fDrv];
    cfg.filterLowCutHz = out[D_fLoCut];
    cfg.filterEnvDepth = out[D_fEnv];
    cfg.filter2Cutoff  = out[D_f2Cut];
    cfg.filter2Res     = out[D_f2Res];
    cfg.filter2Drive   = out[D_f2Drv];
    cfg.filter2LowCutHz= out[D_f2LoCut];
    cfg.filter2EnvDepth= out[D_f2Env];
    cfg.levelDb        = out[D_level];

    // The insert's four slot parameters (0..1 ranges, so the resolved value is the clamped proportion).
    insP = { out[D_insP1], out[D_insP2], out[D_insP3], out[D_insP4] };

    // Publish post-matrix values for the UI live-arc indicators (same units as the bound knobs: filter cutoff
    // in Hz, xmod 0..100, levels in dB). The two filter-2 dests have no live-arc.
    auto& s = modLive;
    auto put = [&s](int k, float v) { s[(size_t) k].store(v, std::memory_order_relaxed); };
    put(kTantSnapOsc1Octave,   out[D_o1Oct]);
    put(kTantSnapOsc1Semi,     out[D_o1Semi]);
    put(kTantSnapOsc1Fine,     out[D_o1Fine]);
    put(kTantSnapOsc1Pos,      out[D_o1Pos]);
    put(kTantSnapOsc2Octave,   out[D_o2Oct]);
    put(kTantSnapOsc2Semi,     out[D_o2Semi]);
    put(kTantSnapOsc2Fine,     out[D_o2Fine]);
    put(kTantSnapOsc2Pos,      out[D_o2Pos]);
    put(kTantSnapXModIndex,    out[D_xmIndex]);
    put(kTantSnapXModDepth,    out[D_xmDepth]);
    put(kTantSnapXModSsb,      out[D_xmSsb]);
    put(kTantSnapOsc1Level,    out[D_o1Lvl]);
    put(kTantSnapOsc2Level,    out[D_o2Lvl]);
    put(kTantSnapNoiseLevel,   out[D_nzLvl]);
    put(kTantSnapFilterCutoff, out[D_fCut]);   // actual Hz
    put(kTantSnapFilterRes,    out[D_fRes]);
    put(kTantSnapLevel,        out[D_level]);
    put(kTantSnapInsP1,        out[D_insP1]);
    put(kTantSnapInsP2,        out[D_insP2]);
    put(kTantSnapInsP3,        out[D_insP3]);
    put(kTantSnapInsP4,        out[D_insP4]);
}

void Pattern::applyFilterEnvelope(bool playing, double beat, double sampleRate, int n, VoiceConfig& cfg) noexcept
{
    // Filter pattern envelopes add to the cutoff in proportion space (envelope 0 → no change from base; 1 at
    // depth 1 → fully open), with the gate's kMinAttackMs rise limit so gate and filter envelopes that are out
    // of sync do not click.
    auto& fPat = filterGate;
    if (playing && fPat.hasEnvelopes.load(std::memory_order_relaxed))
    {
        // The cutoff is a per-BLOCK value, so the de-click slew (a per-sample rate from kMinAttackMs) is scaled
        // by the block length; otherwise a per-bar decay envelope would never re-open the filter.
        const float maxRise = (sampleRate > 0.0 && GatePattern::kMinAttackMs > 0.0f)
                            ? (float) ((double) juce::jmax(1, n)
                                       / ((double) GatePattern::kMinAttackMs * 0.001 * sampleRate))
                            : 1.0f;
        if (mu_core::trySpinLock(fPat.editLock))
        {
            fPat.resetGateCache();
            const float target = fPat.gateAt(beat, 0.0f);
            fPat.filterLevel = (target > fPat.filterLevel) ? std::min(target, fPat.filterLevel + maxRise) : target;

            const float depth    = juce::jlimit(-1.0f, 1.0f, cfg.filterEnvDepth);
            const float baseProp = tantPropFromCutoff(cfg.filterCutoff);
            const float modProp  = juce::jlimit(0.0f, 1.0f, baseProp + depth * fPat.filterLevel);
            cfg.filterCutoff = juce::jlimit(20.0f, 20000.0f, tantCutoffFromProp(modProp));

            // The same envelope on Filter 2 with its own depth.
            const float depth2    = juce::jlimit(-1.0f, 1.0f, cfg.filter2EnvDepth);
            const float baseProp2 = tantPropFromCutoff(cfg.filter2Cutoff);
            const float modProp2  = juce::jlimit(0.0f, 1.0f, baseProp2 + depth2 * fPat.filterLevel);
            cfg.filter2Cutoff = juce::jlimit(20.0f, 20000.0f, tantCutoffFromProp(modProp2));

            mu_core::spinUnlock(fPat.editLock);
        }
        // On lock contention: the un-modulated cutoffs (an edit-time blip).
    }
    else if (! playing)
    {
        fPat.filterLevel = 1.0f;   // reset when stopped
    }
}

void Pattern::applyPitchEnvelope(bool playing, double beat, VoiceConfig& cfg) noexcept
{
    // The pitch envelope (0..1) × depth (±24 semitones, after modulation) shifts both oscillators, smoothly.
    auto& pPat = pitchGate;
    if (playing && pPat.hasEnvelopes.load(std::memory_order_relaxed))
    {
        if (mu_core::trySpinLock(pPat.editLock))
        {
            pPat.resetGateCache();
            const float envLevel = pPat.gateAt(beat, 0.0f);
            cfg.osc1SemiMod += cfg.osc1PitchEnvDepth * envLevel;
            cfg.osc2SemiMod += cfg.osc2PitchEnvDepth * envLevel;
            mu_core::spinUnlock(pPat.editLock);
        }
    }
}

// The layer's own Note-mode gate (a host without a final-mix gate): an 8 ms linear ramp toward 1 while a key is
// held, 0 when none is, on the layer's output. Off in the mu-Tant standalone, which gates the final mix.
void Pattern::applyOwnNoteGate(juce::AudioBuffer<float>& out, int n, int mode, double sampleRate) noexcept
{
    if (mode != 1) return;
    const float target   = notes.held.numHeld() > 0 ? 1.0f : 0.0f;
    const float gainStep = 1.0f / (float) juce::jmax(1.0, kNoteGateFadeSecs * sampleRate);
    float* l = out.getWritePointer(0);
    float* r = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    float  g = notes.gateGain;
    for (int i = 0; i < n; ++i)
    {
        g += juce::jlimit(-gainStep, gainStep, target - g);
        l[i] *= g;
        if (r != nullptr) r[i] *= g;
    }
    notes.gateGain = g;
}

// One voice's full chain into its slot's buffer: modulation → engine → gate → insert. Real-time: no
// allocation (the map is pre-sized, the engine and insert were prepared), no parameter by id, and the only
// locks are try-locks (the wavetable bank, the envelopes' and the matrix's spin locks): a missed bank lock
// renders this voice silent for the call, nothing waits.
void Pattern::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    const int mode = hearNotes(ctx);
    if (parked) { haveResolved = false; parked = false; }   // just gone live: hold nothing from before (C12)
    if (engine == nullptr) return;

    // The engine reads the wavetable bank, which a user-wavetable import appends to under bankLock (same acquire
    // order as the processor's block lock always was: layersLock, then bankLock).
    const BankTryLock bankTry(bankLock);
    if (! bankTry.isLocked()) return;

    // The layer's own 64-beat space: exact on a host that already bounds the beat (mu-Tant's resolver), and
    // correct in an unbounded one, negative pre-roll beats included.
    const double beat = mu_core::wrapBeat(ctx.startBeat, kBeatSpace);
    const bool   playing = ctx.playing != 0;

    VoiceConfig cfg = readConfig();
    std::array<float, 4> insP {};
    applyModulation(beat, cfg, insP);
    applyFilterEnvelope(playing, beat, ctx.sampleRate, n, cfg);
    applyPitchEnvelope(playing, beat, cfg);

    // Note-mode pitch track: transpose the voice so its tonal centre lands on the held MIDI note. Free-mode
    // centre = root + 12*kBaseOctave (octave 0, root → C3); scale intervals + per-osc oct/semi/fine still apply.
    if (mode == 1 && notes.held.newest() >= 0)
        cfg.pitchOffsetSemis = (float) (notes.held.newest() - (cfg.root + 12 * kBaseOctave));

    lastPitchOffset = cfg.pitchOffsetSemis;
    engine->setConfig(cfg);
    engine->process(out, n);

    // The gater shapes the (post-filter) output: bypassed → passes; stopped → silence; playing with no
    // envelopes → silence; playing with envelopes → the per-sample envelope (applyGateBlock).
    const float gateGap    = rows.gateGap.load() * 0.01f;   // 0..100% → 0..1
    const bool  gateBypass = rows.gateBypass.load() > 0.5f;
    float* gl = out.getWritePointer(0);
    float* gr = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    applyGateBlock(gate, gl, gr, n, gateGap, gateBypass, playing, beat, ctx.beatsPerSample, ctx.sampleRate);
    lastGateOpen = gateBypass || (playing && gate.hasEnvelopes.load(std::memory_order_relaxed));

    // The insert with its modulated slot parameters.
    VoiceParams ip;
    ip.insertAlgo     = (int) rows.drvChar.load();
    ip.insertParam[0] = insP[0];
    ip.insertParam[1] = insP[1];
    ip.insertParam[2] = insP[2];
    ip.insertParam[3] = insP[3];
    insert->process(out, n, out.getNumChannels(), ip);

    // The layer's own Note-mode gate, on its final output (off in the standalone, which gates the final mix).
    if (gatesOwnOutput) applyOwnNoteGate(out, n, mode, ctx.sampleRate);

    // Feed the sidebar spectrum glyph with the post-insert audio.
    ring.write(out, n);
}

void Pattern::onParkedBlock(const mu_core::RenderContext& ctx)
{
    hearNotes(ctx);
    parked = true;
}

mu_core::RetirePolicy Pattern::retirePolicy() const
{
    mu_core::RetirePolicy p;
    p.kind   = mu_core::RetirePolicy::Kind::Fade;
    p.fadeMs = 25.0f;
    return p;
}

void Pattern::beginRetire()
{
    auto& s = retireSnap.value;
    s.config  = readConfig();   // the OLD osc + filter, unmodulated
    s.insAlgo = (int) rows.drvChar.load();
    s.insP    = { juce::jlimit(0.0f, 1.0f, rows.insP1.load()), juce::jlimit(0.0f, 1.0f, rows.insP2.load()),
                  juce::jlimit(0.0f, 1.0f, rows.insP3.load()), juce::jlimit(0.0f, 1.0f, rows.insP4.load()) };
}

// The tail of a voice a count-reducing preset dropped: the snapshot config through the slot's (intact) engine
// under the bin's falling gain, then the snapshot insert, so an effect tail the new preset drops fades too.
// Reads only the snapshot, the engine and the insert (C3).
bool Pattern::renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n,
                             mu_core::RetireState& rs)
{
    juce::ignoreUnused(ctx);
    if (engine == nullptr) return false;
    const BankTryLock bankTry(bankLock);
    if (! bankTry.isLocked()) return true;   // contended: silent this call, the fade runs on

    // The slot is empty of a live layer (an in-place tail renders only then), so the tail writes `out`; a tail
    // that shares the slot with something else renders aside and adds. mu-Tant's tails are in-place (a spare in its
    // own slot); a host that replaces the type of a slot (mu-Tronic) parks the whole layer, and its tail then adds
    // to whatever the slot's new layer renders.
    juce::AudioBuffer<float> aside;
    if (rs.exclusive == 0)
    {
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), juce::jmin(2, out.getNumChannels()), juce::jmin(n, tailScratch.getNumSamples()));
        aside.clear();
    }
    auto& buf = rs.exclusive != 0 ? out : aside;
    const int frames = juce::jmin(n, buf.getNumSamples());

    // The live layer's last block decides what the tail may sound: a closed pattern gate, or Note mode on the layer's
    // own output with no key held, was silent, and a tail that droned on would be a burst. The held key's pitch is
    // kept. (The gain is the one the live layer last rendered with: nothing advances it once the layer is parked.)
    const bool  noteGated = gatesOwnOutput && noteMode.load(std::memory_order_relaxed) == 1;
    const float tailGain  = noteGated ? notes.gateGain : 1.0f;
    if (! lastGateOpen || tailGain <= 0.0f)
    {
        if (rs.exclusive != 0)
            for (int c = 0; c < out.getNumChannels(); ++c) juce::FloatVectorOperations::clear(out.getWritePointer(c), n);
        return true;   // the bin ends the fade when its ramp is spent
    }

    const auto& s = retireSnap.value;
    auto cfg = s.config;
    cfg.pitchOffsetSemis = lastPitchOffset;
    engine->setConfig(cfg);
    engine->process(buf, frames);
    if (tailGain < 1.0f) buf.applyGain(0, frames, tailGain);
    mu_core::applyLinearFade(buf, frames, rs);

    VoiceParams ip;
    ip.insertAlgo     = s.insAlgo;
    ip.insertParam[0] = s.insP[0];
    ip.insertParam[1] = s.insP[1];
    ip.insertParam[2] = s.insP[2];
    ip.insertParam[3] = s.insP[3];
    insert->process(buf, frames, buf.getNumChannels(), ip);

    if (rs.exclusive == 0)
        for (int c = 0; c < buf.getNumChannels(); ++c) out.addFrom(c, 0, buf, c, 0, frames);
    return true;   // the bin ends the fade when its ramp is spent
}

void Pattern::resetSlotState() noexcept
{
    for (auto& v : modLive) v.store(0.0f, std::memory_order_relaxed);
    haveResolved = false;
}

} // namespace mu_tant
