// SampleRhythm — the per-block modulation pass (audio thread): seed each destination, run the
// matrix, hand the mixer its strip values, publish the UI arcs, write the values back. Moved from
// the processor (E5.4a); the processor still calls it once per live rhythm per block.

#include "Sequencer/SampleRhythm.h"
#include <cmath>
#include "Audio/SpinLock.h"            // mu-core: spin lock helpers
#include "Audio/InsertSlotConfig.h"
#include "Modulation/ModulationSkew.h"     // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Modulation/LaneModulation.h"  // mu-core: the shared per-lane resolve

namespace mu_clid {

// Each modulation destination this file reads or writes, as its ModDest::kTable index (compile
// time). applyModulation reaches the values through the lane (base() / out()) — no per-block key hashing.
namespace md
{
    constexpr int accentDb          = ModDest::indexOf("accentDb");
    constexpr int amp_attack        = ModDest::indexOf("amp.attack");
    constexpr int amp_decay         = ModDest::indexOf("amp.decay");
    constexpr int amp_level         = ModDest::indexOf("amp.level");
    constexpr int amp_pan           = ModDest::indexOf("amp.pan");
    constexpr int amp_sustain       = ModDest::indexOf("amp.sustain");
    constexpr int euclid_a_hits     = ModDest::indexOf("euclid.a.hits");
    constexpr int euclid_a_insLen   = ModDest::indexOf("euclid.a.insLen");
    constexpr int euclid_a_insSt    = ModDest::indexOf("euclid.a.insSt");
    constexpr int euclid_a_postPad  = ModDest::indexOf("euclid.a.postPad");
    constexpr int euclid_a_prePad   = ModDest::indexOf("euclid.a.prePad");
    constexpr int euclid_a_rotate   = ModDest::indexOf("euclid.a.rotate");
    constexpr int euclid_b_hits     = ModDest::indexOf("euclid.b.hits");
    constexpr int euclid_b_insLen   = ModDest::indexOf("euclid.b.insLen");
    constexpr int euclid_b_insSt    = ModDest::indexOf("euclid.b.insSt");
    constexpr int euclid_b_postPad  = ModDest::indexOf("euclid.b.postPad");
    constexpr int euclid_b_prePad   = ModDest::indexOf("euclid.b.prePad");
    constexpr int euclid_b_rotate   = ModDest::indexOf("euclid.b.rotate");
    constexpr int euclid_c_hits     = ModDest::indexOf("euclid.c.hits");
    constexpr int euclid_c_insLen   = ModDest::indexOf("euclid.c.insLen");
    constexpr int euclid_c_insSt    = ModDest::indexOf("euclid.c.insSt");
    constexpr int euclid_c_postPad  = ModDest::indexOf("euclid.c.postPad");
    constexpr int euclid_c_prePad   = ModDest::indexOf("euclid.c.prePad");
    constexpr int euclid_c_rotate   = ModDest::indexOf("euclid.c.rotate");
    constexpr int fenv_attack       = ModDest::indexOf("fenv.attack");
    constexpr int fenv_decay        = ModDest::indexOf("fenv.decay");
    constexpr int fenv_depth        = ModDest::indexOf("fenv.depth");
    constexpr int filter_cutoff     = ModDest::indexOf("filter.cutoff");
    constexpr int filter_lowCut     = ModDest::indexOf("filter.lowCut");
    constexpr int filter_resonance  = ModDest::indexOf("filter.resonance");
    constexpr int insert_p1         = ModDest::indexOf("insert.p1");
    constexpr int insert_p2         = ModDest::indexOf("insert.p2");
    constexpr int insert_p3         = ModDest::indexOf("insert.p3");
    constexpr int insert_p4         = ModDest::indexOf("insert.p4");
    constexpr int pitch_envDepth    = ModDest::indexOf("pitch.envDepth");
    constexpr int pitch_octave      = ModDest::indexOf("pitch.octave");
    constexpr int pitch_semitones   = ModDest::indexOf("pitch.semitones");
    constexpr int send_delay        = ModDest::indexOf("send.delay");
    constexpr int send_effect       = ModDest::indexOf("send.effect");
    constexpr int send_reverb       = ModDest::indexOf("send.reverb");
    static_assert(accentDb >= 0, "accentDb is not in ModDest::kTable");
    static_assert(amp_attack >= 0, "amp.attack is not in ModDest::kTable");
    static_assert(amp_decay >= 0, "amp.decay is not in ModDest::kTable");
    static_assert(amp_level >= 0, "amp.level is not in ModDest::kTable");
    static_assert(amp_pan >= 0, "amp.pan is not in ModDest::kTable");
    static_assert(amp_sustain >= 0, "amp.sustain is not in ModDest::kTable");
    static_assert(euclid_a_hits >= 0, "euclid.a.hits is not in ModDest::kTable");
    static_assert(euclid_a_insLen >= 0, "euclid.a.insLen is not in ModDest::kTable");
    static_assert(euclid_a_insSt >= 0, "euclid.a.insSt is not in ModDest::kTable");
    static_assert(euclid_a_postPad >= 0, "euclid.a.postPad is not in ModDest::kTable");
    static_assert(euclid_a_prePad >= 0, "euclid.a.prePad is not in ModDest::kTable");
    static_assert(euclid_a_rotate >= 0, "euclid.a.rotate is not in ModDest::kTable");
    static_assert(euclid_b_hits >= 0, "euclid.b.hits is not in ModDest::kTable");
    static_assert(euclid_b_insLen >= 0, "euclid.b.insLen is not in ModDest::kTable");
    static_assert(euclid_b_insSt >= 0, "euclid.b.insSt is not in ModDest::kTable");
    static_assert(euclid_b_postPad >= 0, "euclid.b.postPad is not in ModDest::kTable");
    static_assert(euclid_b_prePad >= 0, "euclid.b.prePad is not in ModDest::kTable");
    static_assert(euclid_b_rotate >= 0, "euclid.b.rotate is not in ModDest::kTable");
    static_assert(euclid_c_hits >= 0, "euclid.c.hits is not in ModDest::kTable");
    static_assert(euclid_c_insLen >= 0, "euclid.c.insLen is not in ModDest::kTable");
    static_assert(euclid_c_insSt >= 0, "euclid.c.insSt is not in ModDest::kTable");
    static_assert(euclid_c_postPad >= 0, "euclid.c.postPad is not in ModDest::kTable");
    static_assert(euclid_c_prePad >= 0, "euclid.c.prePad is not in ModDest::kTable");
    static_assert(euclid_c_rotate >= 0, "euclid.c.rotate is not in ModDest::kTable");
    static_assert(fenv_attack >= 0, "fenv.attack is not in ModDest::kTable");
    static_assert(fenv_decay >= 0, "fenv.decay is not in ModDest::kTable");
    static_assert(fenv_depth >= 0, "fenv.depth is not in ModDest::kTable");
    static_assert(filter_cutoff >= 0, "filter.cutoff is not in ModDest::kTable");
    static_assert(filter_lowCut >= 0, "filter.lowCut is not in ModDest::kTable");
    static_assert(filter_resonance >= 0, "filter.resonance is not in ModDest::kTable");
    static_assert(insert_p1 >= 0, "insert.p1 is not in ModDest::kTable");
    static_assert(insert_p2 >= 0, "insert.p2 is not in ModDest::kTable");
    static_assert(insert_p3 >= 0, "insert.p3 is not in ModDest::kTable");
    static_assert(insert_p4 >= 0, "insert.p4 is not in ModDest::kTable");
    static_assert(pitch_envDepth >= 0, "pitch.envDepth is not in ModDest::kTable");
    static_assert(pitch_octave >= 0, "pitch.octave is not in ModDest::kTable");
    static_assert(pitch_semitones >= 0, "pitch.semitones is not in ModDest::kTable");
    static_assert(send_delay >= 0, "send.delay is not in ModDest::kTable");
    static_assert(send_effect >= 0, "send.effect is not in ModDest::kTable");
    static_assert(send_reverb >= 0, "send.reverb is not in ModDest::kTable");
}

SampleRhythm::SampleRhythm()
{
    // Pre-populate the modulation map from the target table, so a new row can never make the audio thread insert.
    modParamValues.reserve((size_t) ModDest::kTableSize);
    for (const auto& t : ModDest::kTable)
        if (t.param != nullptr)
            modParamValues[t.id] = 0.0f;

    // The lane: every live (non-reserved) row of the target table, with its knob range. A destination is seeded as its knob's
    // proportion and written back through the same range, so a modulation depth is a % of the knob's travel whatever its skew.
    using mu_clid::mod_skew::LinRange;
    using Range = juce::NormalisableRange<float>;
    auto lin = [](LinRange r) { return Range(r.lo, r.hi); };
    // The skewed knobs keep their exact curves (ModulationSkew.h): ADSR times 0..10 s, filter low cut 0..1000 Hz, filter cutoff 20..20000 Hz.
    const Range adsr(0.0f, 10.0f, [](float, float, float p) { return mod_skew::adsrFromProp(p); },
                                  [](float, float, float v) { return mod_skew::propFromAdsr(v); });
    const Range lowCut(0.0f, 1000.0f, [](float, float, float p) { return mod_skew::lowCutFromProp(p); },
                                      [](float, float, float v) { return mod_skew::propFromLowCut(v); });
    const Range cutoff(20.0f, 20000.0f, [](float, float, float p) { return mod_skew::cutoffFromProp(p); },
                                        [](float, float, float v) { return mod_skew::propFromCutoff(v); });
    laneOfDest.fill(-1);
    for (int i = 0; i < ModDest::kTableSize; ++i)
        if (ModDest::kTable[i].param != nullptr)
        {
            laneOfDest[(size_t) i] = laneCount;
            laneIds[(size_t) laneCount] = ModDest::kTable[i].id;
            laneRange[(size_t) laneCount] = Range(0.0f, 1.0f);
            ++laneCount;
        }
    for (int d : { md::amp_attack, md::amp_decay, md::fenv_attack, md::fenv_decay }) range(d) = adsr;
    range(md::amp_sustain)      = lin(mod_skew::kSustain);
    range(md::filter_cutoff)    = cutoff;
    range(md::filter_resonance) = lin(mod_skew::kResonance);
    range(md::filter_lowCut)    = lowCut;
    range(md::fenv_depth)       = lin(mod_skew::kFenvDepth);
    range(md::pitch_semitones)  = lin(mod_skew::kPitchSemi);
    range(md::pitch_octave)     = lin(mod_skew::kPitchOctave);
    range(md::pitch_envDepth)   = lin(mod_skew::kPitchEnvDepth);
    range(md::amp_level)        = lin(mod_skew::kAmpLevel);
    range(md::accentDb)         = lin(mod_skew::kAccent);
    range(md::amp_pan)          = Range(-1.0f, 1.0f);
    // The insert slots (normalised), the sends and the Euclid knobs stay 0..1; the Euclid ranges are set every block (fillLaneBase).
}

EuclidOverrides SampleRhythm::modulatedEuclidOverrides() const noexcept
{
    // Before a modulation pass has written the overrides they are zeros, so with no assignment the
    // rhythm's own generator values stand in: getStepTypes(ov) then yields the unmodulated pattern.
    if (modulationMatrix.getAssignments().empty())
    {
        EuclidOverrides ov;
        auto fill = [](EuclidGenOverrides& g, const HitGenerator& src) {
            g.hits         = src.hits;
            g.rotate       = src.rotate;
            g.prePad       = src.prePad;
            g.postPad      = src.postPad;
            g.insertStart  = src.insertStart;
            g.insertLength = src.insertLength;
        };
        fill(ov.a, genA);
        fill(ov.b, genB);
        fill(ov.c, genC);
        return ov;
    }
    return lastEuclidOverrides;
}

void SampleRhythm::resetModulationMemory() noexcept
{
    prevMatrixHadAssignments = false;
    prevEuclidOverrides = EuclidOverrides{};
    lastEuclidOverrides = EuclidOverrides{};
}

// The Insert Start knob's range for a generator's current layout ([lo, hi], as EuclideanPanel sets it).
// Modulation depth for Insert Start is a proportion of this range, like the pad knobs.
static std::pair<int, int> insertStartKnobRange(const HitGenerator& g) noexcept
{
    const auto lay = g.clampLayout({ g.hits, g.rotate, g.prePad, g.postPad, g.insertStart, g.insertLength });
    return HitGenerator::insertStartBounds(g.steps, lay.prePad, lay.postPad, lay.insertLength, g.prePadMode);
}

// A step-count dependent knob's range for this block: [lo, hi], never empty (an empty range cannot be converted to a proportion).
static void setDynamicRange(juce::NormalisableRange<float>& r, float lo, float hi) noexcept
{
    r.start = lo;
    r.end   = hi > lo ? hi : lo + 1e-3f;
}

// Phase 1 — every destination's current value in its own units, and this block's Euclid ranges. Values come from the rhythm's
// own fields (a staged preset applies there at its boundary, the parameters may lag them).
void SampleRhythm::fillLaneBase(const VoiceParams& modParams, const MixerEngine::ChannelState& strip)
{
    const Rhythm& rhythm = *this;
    base(md::amp_attack)       = modParams.ampEnvAtk;
    base(md::amp_decay)        = modParams.ampEnvDec;
    base(md::amp_sustain)      = modParams.ampEnvSus;
    // amp.release is not a modulation target (no note-off on a step trigger, so the release stage is never entered).
    base(md::filter_cutoff)    = modParams.filterCutoff;
    base(md::filter_resonance) = modParams.filterRes;
    base(md::fenv_attack)      = modParams.filterEnvAtk;
    base(md::fenv_decay)       = modParams.filterEnvDec;
    base(md::fenv_depth)       = modParams.filterEnvDepth;
    base(md::filter_lowCut)    = modParams.filterLowCutHz;
    base(md::pitch_semitones)  = (float) modParams.pitchSemitones;
    base(md::pitch_octave)     = (float) modParams.pitchOctave;
    // The four generic insert slots, normalised 0..1; each algorithm de-normalises in its own process() through the config table.
    base(md::insert_p1) = modParams.insertParam[0];
    base(md::insert_p2) = modParams.insertParam[1];
    base(md::insert_p3) = modParams.insertParam[2];
    base(md::insert_p4) = modParams.insertParam[3];
    base(md::pitch_envDepth)   = modParams.pitchEnvDepth;
    base(md::amp_level)        = modParams.ampLevel;
    base(md::accentDb)         = modParams.accentDb;

    // Euclid: hits / rotate / pads / insert start and length are proportions of their knobs' CURRENT ranges (the step count and the
    // pad maxima), so 100% modulation is a full knob turn whatever the layout.
    struct Gen { const HitGenerator& g; int hits, rot, pre, post, insSt, insLen; };
    const Gen gens[3] = {
        { rhythm.genA, md::euclid_a_hits, md::euclid_a_rotate, md::euclid_a_prePad, md::euclid_a_postPad, md::euclid_a_insSt, md::euclid_a_insLen },
        { rhythm.genB, md::euclid_b_hits, md::euclid_b_rotate, md::euclid_b_prePad, md::euclid_b_postPad, md::euclid_b_insSt, md::euclid_b_insLen },
        { rhythm.genC, md::euclid_c_hits, md::euclid_c_rotate, md::euclid_c_prePad, md::euclid_c_postPad, md::euclid_c_insSt, md::euclid_c_insLen } };
    for (const auto& e : gens)
    {
        const int steps = juce::jmax(1, e.g.steps);
        const auto padMax = e.g.padKnobMaxima();
        const auto insRange = insertStartKnobRange(e.g);
        setDynamicRange(range(e.hits),   0.0f, (float) steps);
        setDynamicRange(range(e.rot),    0.0f, (float) (steps - 1));
        setDynamicRange(range(e.pre),    0.0f, (float) padMax.prePad);
        setDynamicRange(range(e.post),   0.0f, (float) padMax.postPad);
        setDynamicRange(range(e.insSt),  (float) insRange.first, (float) insRange.second);
        setDynamicRange(range(e.insLen), 0.0f, (float) padMax.insertLength);
        base(e.hits)   = (float) e.g.hits;
        base(e.rot)    = (float) e.g.rotate;
        base(e.pre)    = (float) e.g.prePad;
        base(e.post)   = (float) e.g.postPad;
        base(e.insSt)  = (float) juce::jlimit(insRange.first, insRange.second, e.g.insertStart);
        base(e.insLen) = (float) e.g.insertLength;
    }

    // Mixer strip: pan and the FX sends.
    base(md::amp_pan)     = strip.pan.load(std::memory_order_relaxed);
    base(md::send_effect) = strip.sendEffect.load(std::memory_order_relaxed);
    base(md::send_delay)  = strip.sendDelay.load(std::memory_order_relaxed);
    base(md::send_reverb) = strip.sendReverb.load(std::memory_order_relaxed);
}

// Phase 3 — hand the modulated pan / sends to the mixer channel for this block.
SampleRhythm::StripMod SampleRhythm::applyStripModulation(MixerEngine::ChannelState& strip)
{
    const float modPan = juce::jlimit(-1.0f, 1.0f, out(md::amp_pan));
    const float modEff = juce::jlimit(0.0f, 1.0f, out(md::send_effect));
    const float modDly = juce::jlimit(0.0f, 1.0f, out(md::send_delay));
    const float modRev = juce::jlimit(0.0f, 1.0f, out(md::send_reverb));
    strip.panMod       .store(modPan, std::memory_order_relaxed);
    strip.sendEffectMod.store(modEff, std::memory_order_relaxed);
    strip.sendDelayMod .store(modDly, std::memory_order_relaxed);
    strip.sendReverbMod.store(modRev, std::memory_order_relaxed);
    return { modPan, modEff, modDly, modRev };
}

// Phase 4 — publish each destination's modulated ACTUAL value for the knobs' live arcs.
void SampleRhythm::publishModSnapshot(const VoiceParams& modParams, const StripMod& stripMod)
{
    auto& snap = modSnapshot;
    // The knob's proportion of a modulated value, for the arcs that are drawn in proportion.
    auto prop = [this](int d) { return juce::jlimit(0.0f, 1.0f, range(d).convertTo0to1(out(d))); };

    snap[kSnapAmpAtk]      .store(out(md::amp_attack));
    snap[kSnapAmpDec]      .store(out(md::amp_decay));
    snap[kSnapAmpSus]      .store(prop(md::amp_sustain));
    snap[kSnapFilterCutoff].store(out(md::filter_cutoff));
    snap[kSnapFilterRes]   .store(prop(md::filter_resonance));
    snap[kSnapFenvAtk]     .store(out(md::fenv_attack));
    snap[kSnapFenvDec]     .store(out(md::fenv_decay));
    snap[kSnapFenvDepth]   .store(out(md::fenv_depth));        // semitones 0..48
    snap[kSnapPitchSemi]   .store(out(md::pitch_semitones));   // the modulated knob position (base plus modulation)
    // Insert slots store the ACTUAL slider value of the active algorithm, so setModulatedActual maps it through the slider's own curve.
    const int algForSnap = (int) modParams.insertAlgo;
    snap[kSnapInsP1].store(mu_ui::normToActual(out(md::insert_p1), algForSnap, 0));
    snap[kSnapInsP2].store(mu_ui::normToActual(out(md::insert_p2), algForSnap, 1));
    snap[kSnapInsP3].store(mu_ui::normToActual(out(md::insert_p3), algForSnap, 2));
    snap[kSnapInsP4].store(mu_ui::normToActual(out(md::insert_p4), algForSnap, 3));
    snap[kSnapPitchEnvDep] .store(out(md::pitch_envDepth));    // semitones 0..24
    snap[kSnapAmpLvl]      .store(out(md::amp_level));         // dB -60..+6
    snap[kSnapAccent]      .store(out(md::accentDb));          // dB 0..12
    snap[kSnapFilterLowCut].store(out(md::filter_lowCut));
    snap[kSnapPitchOctave] .store(out(md::pitch_octave));
    snap[kSnapPan]         .store(stripMod.pan);
    snap[kSnapSendEff]     .store(stripMod.effect);
    snap[kSnapSendDly]     .store(stripMod.delay);
    snap[kSnapSendRev]     .store(stripMod.reverb);
    // Euclid: hits / rotate / insert start as the knob's proportion (setModulatedNorm); pads and insert length as the step value.
    snap[kSnapEucAHits]    .store(prop(md::euclid_a_hits));
    snap[kSnapEucARotate]  .store(prop(md::euclid_a_rotate));
    snap[kSnapEucAPrePad]  .store(out(md::euclid_a_prePad));
    snap[kSnapEucAPostPad] .store(out(md::euclid_a_postPad));
    snap[kSnapEucAInsSt]   .store(prop(md::euclid_a_insSt));
    snap[kSnapEucAInsLen]  .store(out(md::euclid_a_insLen));
    snap[kSnapEucBHits]    .store(prop(md::euclid_b_hits));
    snap[kSnapEucBRotate]  .store(prop(md::euclid_b_rotate));
    snap[kSnapEucBPrePad]  .store(out(md::euclid_b_prePad));
    snap[kSnapEucBPostPad] .store(out(md::euclid_b_postPad));
    snap[kSnapEucBInsSt]   .store(prop(md::euclid_b_insSt));
    snap[kSnapEucBInsLen]  .store(out(md::euclid_b_insLen));
    snap[kSnapEucCHits]    .store(prop(md::euclid_c_hits));
    snap[kSnapEucCRotate]  .store(prop(md::euclid_c_rotate));
    snap[kSnapEucCPrePad]  .store(out(md::euclid_c_prePad));
    snap[kSnapEucCPostPad] .store(out(md::euclid_c_postPad));
    snap[kSnapEucCInsSt]   .store(prop(md::euclid_c_insSt));
    snap[kSnapEucCInsLen]  .store(out(md::euclid_c_insLen));
}

// Phase 5 — write the modulated values back: the voice params for this block, and the Euclid overrides (whole steps) that drive
// the pattern recompute.
void SampleRhythm::writeBackModulation(VoiceParams& modParams)
{
    modParams.ampEnvAtk      = juce::jmax(0.001f, out(md::amp_attack));
    modParams.ampEnvDec      = juce::jmax(0.001f, out(md::amp_decay));
    modParams.ampEnvSus      = out(md::amp_sustain);
    modParams.filterCutoff   = juce::jlimit(20.0f, 20000.0f, out(md::filter_cutoff));
    modParams.filterRes      = out(md::filter_resonance);
    modParams.filterEnvAtk   = juce::jmax(0.001f, out(md::fenv_attack));
    modParams.filterEnvDec   = juce::jmax(0.001f, out(md::fenv_decay));
    modParams.filterEnvDepth = out(md::fenv_depth);
    modParams.filterLowCutHz = out(md::filter_lowCut);
    // One pitch offset in semitones: how far the octave and semitone knobs were moved from their own values (12 semitones an octave).
    modParams.pitchMod       = juce::jlimit(-48.0f, 48.0f,
                                            (out(md::pitch_octave)    - modParams.pitchOctave) * 12.0f
                                          + (out(md::pitch_semitones) - modParams.pitchSemitones));
    modParams.insertParam[0] = juce::jlimit(0.0f, 1.0f, out(md::insert_p1));
    modParams.insertParam[1] = juce::jlimit(0.0f, 1.0f, out(md::insert_p2));
    modParams.insertParam[2] = juce::jlimit(0.0f, 1.0f, out(md::insert_p3));
    modParams.insertParam[3] = juce::jlimit(0.0f, 1.0f, out(md::insert_p4));
    modParams.pitchEnvDepth  = out(md::pitch_envDepth);
    modParams.ampLevel       = out(md::amp_level);
    modParams.accentDb       = out(md::accentDb);

    // Euclid values to whole steps, each held inside what the pattern allows.
    const Rhythm& rhythm = *this;
    auto toOverrides = [this](EuclidGenOverrides& o, const HitGenerator& g, int hits, int rot, int pre, int post, int insSt, int insLen)
    {
        const int steps = juce::jmax(1, g.steps);
        const auto& insR = range(insSt);
        o.hits         = juce::jlimit(0, steps,     juce::roundToInt(out(hits)));
        o.rotate       = juce::jlimit(0, steps - 1, juce::roundToInt(out(rot)));
        o.prePad       = juce::roundToInt(out(pre));
        o.postPad      = juce::roundToInt(out(post));
        o.insertStart  = juce::jlimit(juce::roundToInt(insR.start), juce::roundToInt(insR.start) + juce::jmax(0, (int) std::lround(insR.end - insR.start)),
                                      juce::roundToInt(out(insSt)));
        o.insertLength = juce::roundToInt(out(insLen));
    };
    toOverrides(lastEuclidOverrides.a, rhythm.genA, md::euclid_a_hits, md::euclid_a_rotate, md::euclid_a_prePad, md::euclid_a_postPad, md::euclid_a_insSt, md::euclid_a_insLen);
    toOverrides(lastEuclidOverrides.b, rhythm.genB, md::euclid_b_hits, md::euclid_b_rotate, md::euclid_b_prePad, md::euclid_b_postPad, md::euclid_b_insSt, md::euclid_b_insLen);
    toOverrides(lastEuclidOverrides.c, rhythm.genC, md::euclid_c_hits, md::euclid_c_rotate, md::euclid_c_prePad, md::euclid_c_postPad, md::euclid_c_insSt, md::euclid_c_insLen);
}

void SampleRhythm::applyModulation(double beatPos, MixerEngine::ChannelState& strip, int lastEffectiveStep,
                                   VoiceEngine* engine)
{
    Rhythm& rhythm = *this;

    // gate the modulation pass on "matrix has assignments now, OR had
    // assignments last block". The first half is the normal case. The second half
    // runs one final pass on the block AFTER assignment removal so the write-back
    // re-seeds lastEuclidOverrides to base values; Stage B's change-detection
    // then recomputes the safe pattern back to base. Without that transition pass,
    // a never-modulated rhythm pays no per-block cost, but a rhythm whose last
    // assignment was just removed would leave lastEuclidOverrides stuck on the old
    // modulated values, freezing the pattern.
    const bool matrixHasAssignments = !rhythm.modulationMatrix.getAssignments().empty();
    const bool runModulationPass    = matrixHasAssignments || prevMatrixHadAssignments;

    // Snapshot voiceParams under voiceParamsLock so a concurrent message-thread apply
    // (applyPendingRhythmParams / forceSyncRhythmFromApvts) can't interleave a torn write. Only
    // taken when the pass needs it, and only tried: the message thread holds it for one field apply,
    // so a busy lock means that apply is in flight. Then this block keeps last block's modulated
    // values (the engine's active params, the strip modulation) and the pass runs next block, instead
    // of the audio thread spinning behind a message thread that may have been descheduled.
    VoiceParams modParams;
    if (runModulationPass)
    {
        if (! mu_core::trySpinLock(rhythm.voiceParamsLock))
            return;
        modParams = rhythm.voiceParams;
        mu_core::spinUnlock(rhythm.voiceParamsLock);
    }
    prevMatrixHadAssignments = matrixHasAssignments;

    bool modulationRan = false;
    if (runModulationPass)
    {
        fillLaneBase(modParams, strip);                                                // phase 1
        // Phase 2: seed, run the matrix under its try-lock, back to units. A missed lock leaves the block on last block's values.
        if (mu_mod::resolveLaneSeeded(this, beatPos, laneCount, laneIds.data(), laneRange.data(), modParamValues, laneOut.data(),
                                      nullptr, [this](int k) { return laneBase[(size_t) k]; }))
        {
            modulationRan = true;
            const StripMod stripMod = applyStripModulation(strip);                     // phase 3
            publishModSnapshot(modParams, stripMod);                                   // phase 4
            writeBackModulation(modParams);                                            // phase 5
        }
    }

    // No modulation this block: the mixer uses the strip's own pan / sends.
    if (! runModulationPass)
    {
        for (auto* m : { &strip.panMod, &strip.sendEffectMod, &strip.sendDelayMod, &strip.sendReverbMod })
            m->store(MixerEngine::ChannelState::kNoMod, std::memory_order_relaxed);
    }

    // Stage B: trigger pattern recompute when integer-rounded overrides changed
    // since last block. Skips recompute on every block where modulation is sub-step
    // (typical case for a slow LFO). tryUpdatePatternFromModulation is non-blocking —
    // a missed try-lock just defers to the next block; prevEuclidOverrides only
    // advances when the recompute actually applied so the change-detection retries.
    if (lastEuclidOverrides != prevEuclidOverrides)
    {
        if (cursor.tryApplyModulatedPattern(*this, lastEuclidOverrides, lastEffectiveStep))
            prevEuclidOverrides = lastEuclidOverrides;
    }

    // only override activeParams when the modulation pass actually ran. For an
    // unmodulated rhythm modParams ≡ rhythm.voiceParams ≡ activeParams (already kept in
    // sync by VoiceEngine::applyPendingParams' dirty-flag path), so the call would be
    // pure waste — re-syncing ADSR + filter every block for no change.
    // A missed modLock leaves modParams at the unmodulated base, so pushing it would jump the voice for a block.
    if (modulationRan && engine != nullptr)
        engine->setActiveParams(modParams);
}

} // namespace mu_clid
