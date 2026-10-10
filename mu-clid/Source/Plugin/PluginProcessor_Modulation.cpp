// PluginProcessor — per-rhythm modulation (audio thread): seed each destination, run the
// matrix, hand the mixer its strip values, publish the UI arcs, write the values back.
// Partial-class TU split from PluginProcessor.cpp (like PluginProcessor_APVTS.cpp).

#include "PluginProcessor.h"
#include "PluginProcessor_Internal.h"
#include "Audio/SpinLock.h"            // mu-core: spin lock helpers
#include "Audio/InsertSlotConfig.h"
#include "Modulation/ModulationSkew.h"     // proportion-space skew helpers (shared with test C5)
#include "Modulation/MuClidModDest.h"  // mu-clid modulation targets
#include "Sequencer/Rhythm.h"

#if ! MUCLID_LITE_BUILD

// Each modulation destination this file reads or writes, as its ModDest::kTable index (compile
// time). applyRhythmModulation reaches the values through modSlot[] — no per-block key hashing.
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

// Phase 1 — seed every destination with its knob value as the matrix expects it: a proportion
// of the knob's range for skewed / step-count-dependent knobs, the offset 0 for pitch.
void PluginProcessor::seedModulation(int r, const Rhythm& rhythm, const VoiceParams& modParams)
{
    // PROPORTION-SPACE modulation for skewed-slider destinations:
    // additive-in-display-units modulation on a skewed slider gives
    // variable visual arc length (the same display delta covers a
    // different visual proportion at different knob positions). Seed
    // these destinations as the slider's proportion (0..1), apply
    // modulation additively in proportion-space, then convert back
    // via the slider's skew at write-back. ADSR times use skewFactor
    // 0.3 on a 0..10 range; filter.lowCut uses skewFactor 0.35 on 0..1000.
    // amp.level is dB-linear (-60..+6) so the slider's "proportion" is
    // (dB + 60) / 66 — modulate in dB.
    // Skew conversions (forward + inverse) live in ModulationSkew.h so the
    // seed / snapshot / write-back blocks share one definition (test C5).
    using namespace mu_clid::mod_skew;   // skew conversions + linear knob ranges (kSustain, kPad, ...)

    mv(md::amp_attack)       = propFromAdsr(modParams.ampEnvAtk);
    mv(md::amp_decay)        = propFromAdsr(modParams.ampEnvDec);
    mv(md::amp_sustain)      = kSustain.prop(modParams.ampEnvSus);
    // amp.release is not a modulation target (no note-off on a step
    // trigger, so the release stage is never entered; see Finding 2).
    mv(md::filter_cutoff)    = propFromCutoff(modParams.filterCutoff);  // proportion-space, log-skewed
    mv(md::filter_resonance) = kResonance.prop(modParams.filterRes);
    mv(md::fenv_attack)      = propFromAdsr(modParams.filterEnvAtk);
    mv(md::fenv_decay)       = propFromAdsr(modParams.filterEnvDec);
    mv(md::fenv_depth)       = kFenvDepth.prop(modParams.filterEnvDepth);
    mv(md::filter_lowCut)    = propFromLowCut(modParams.filterLowCutHz);
    // pitch.octave and pitch.semitones are OFFSETS from the knob (seeded 0, in proportion
    // of the knob's range); summed in semitones at write-back → pitchMod.
    mv(md::pitch_semitones)  = 0.0f;
    mv(md::pitch_octave)     = 0.0f;
    // Stage 36: insert mod targets the 4 generic slots directly.
    // Each algorithm's process() converts slot ↔ actual via the
    // per-algo config table; modulation only sees normalised 0..1
    // so the same destination name (`insert.p1`) means "knob 1
    // of the active algorithm" regardless of which algo is loaded.
    mv(md::insert_p1) = modParams.insertParam[0];
    mv(md::insert_p2) = modParams.insertParam[1];
    mv(md::insert_p3) = modParams.insertParam[2];
    mv(md::insert_p4) = modParams.insertParam[3];
    // new destinations
    mv(md::pitch_envDepth)   = kPitchEnvDepth.prop(modParams.pitchEnvDepth);
    mv(md::amp_level)        = kAmpLevel.prop(modParams.ampLevel);
    mv(md::accentDb)         = kAccent.prop(modParams.accentDb);
    // Stage A: seed euclid pattern destinations with base gen values.
    // hits/rotate/insSt use PROPORTION-SPACE modulation because their
    // slider ranges depend on the current step count — proportion-space gives
    // 100%-mod = 100%-knob-turn regardless of step count. prePad/postPad/insLen are
    // likewise proportions of their knobs' current ranges (HitGenerator::padKnobMaxima).
    const int stepsA_seed = juce::jmax(1, rhythm.genA.steps);
    const int stepsB_seed = juce::jmax(1, rhythm.genB.steps);
    const int stepsC_seed = juce::jmax(1, rhythm.genC.steps);
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    auto propOf = [](int v, int max) { return max > 0 ? (float) v / (float) max : 0.0f; };
    mv(md::euclid_a_hits)    = (float) rhythm.genA.hits         / (float) stepsA_seed;
    mv(md::euclid_a_rotate)  = (float) rhythm.genA.rotate       / (float) juce::jmax(1, stepsA_seed - 1);
    mv(md::euclid_a_prePad)  = propOf(rhythm.genA.prePad,       padMaxA.prePad);
    mv(md::euclid_a_postPad) = propOf(rhythm.genA.postPad,      padMaxA.postPad);
    mv(md::euclid_a_insSt)   = (float) rhythm.genA.insertStart  / (float) juce::jmax(1, stepsA_seed - 1);
    mv(md::euclid_a_insLen)  = propOf(rhythm.genA.insertLength, padMaxA.insertLength);
    mv(md::euclid_b_hits)    = (float) rhythm.genB.hits         / (float) stepsB_seed;
    mv(md::euclid_b_rotate)  = (float) rhythm.genB.rotate       / (float) juce::jmax(1, stepsB_seed - 1);
    mv(md::euclid_b_prePad)  = propOf(rhythm.genB.prePad,       padMaxB.prePad);
    mv(md::euclid_b_postPad) = propOf(rhythm.genB.postPad,      padMaxB.postPad);
    mv(md::euclid_b_insSt)   = (float) rhythm.genB.insertStart  / (float) juce::jmax(1, stepsB_seed - 1);
    mv(md::euclid_b_insLen)  = propOf(rhythm.genB.insertLength, padMaxB.insertLength);
    mv(md::euclid_c_hits)    = (float) rhythm.genC.hits         / (float) stepsC_seed;
    mv(md::euclid_c_rotate)  = (float) rhythm.genC.rotate       / (float) juce::jmax(1, stepsC_seed - 1);
    mv(md::euclid_c_prePad)  = propOf(rhythm.genC.prePad,       padMaxC.prePad);
    mv(md::euclid_c_postPad) = propOf(rhythm.genC.postPad,      padMaxC.postPad);
    mv(md::euclid_c_insSt)   = (float) rhythm.genC.insertStart  / (float) juce::jmax(1, stepsC_seed - 1);
    mv(md::euclid_c_insLen)  = propOf(rhythm.genC.insertLength, padMaxC.insertLength);

    // Mixer strip: pan + FX sends, seeded from the mixer channel as proportions of
    // their knobs (pan -1..+1 → 0..1; sends are 0..1 already).
    auto& strip = mixerEngine.channels[(size_t) r];
    mv(md::amp_pan)     = (strip.pan.load(std::memory_order_relaxed) + 1.0f) * 0.5f;
    mv(md::send_effect) = strip.sendEffect.load(std::memory_order_relaxed);
    mv(md::send_delay)  = strip.sendDelay.load(std::memory_order_relaxed);
    mv(md::send_reverb) = strip.sendReverb.load(std::memory_order_relaxed);
}

// Phase 3 — hand the modulated pan / sends to the mixer channel for this block.
PluginProcessor::StripMod PluginProcessor::applyStripModulation(int r)
{
    auto& strip = mixerEngine.channels[(size_t) r];
    // Hand the modulated strip values to the mixer for this block.
    const float modPan = juce::jlimit(-1.0f, 1.0f, mv(md::amp_pan) * 2.0f - 1.0f);
    const float modEff = juce::jlimit(0.0f, 1.0f, mv(md::send_effect));
    const float modDly = juce::jlimit(0.0f, 1.0f, mv(md::send_delay));
    const float modRev = juce::jlimit(0.0f, 1.0f, mv(md::send_reverb));
    strip.panMod       .store(modPan, std::memory_order_relaxed);
    strip.sendEffectMod.store(modEff, std::memory_order_relaxed);
    strip.sendDelayMod .store(modDly, std::memory_order_relaxed);
    strip.sendReverbMod.store(modRev, std::memory_order_relaxed);
    return { modPan, modEff, modDly, modRev };
}

// Phase 4 — publish each destination's modulated ACTUAL value for the knobs' live arcs.
void PluginProcessor::publishModSnapshot(int r, const Rhythm& rhythm, const VoiceParams& modParams,
                                         const StripMod& stripMod)
{
    using namespace mu_clid::mod_skew;
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    const float modPan = stripMod.pan, modEff = stripMod.effect, modDly = stripMod.delay, modRev = stripMod.reverb;
    // Snapshot pre-normalised values for the UI live-arc indicator.
    {
        auto& snap = modSnapshot[r];
        // Proportion-space destinations — modParamValues holds slider proportion 0..1.
        // Snap stores the ACTUAL value (seconds / Hz / dB) so the UI's setModulatedActual
        // routes via valueToProportionOfLength and matches the needle's visual position
        // by construction. Same pattern as filter.cutoff and insert.pN (Stage 36).
        // adsrFromProp / lowCutFromProp / cutoffFromProp come from ModulationSkew.h
        // (brought in via the using-declarations above).
        snap[kSnapAmpAtk]      .store(adsrFromProp(mv(md::amp_attack)));
        snap[kSnapAmpDec]      .store(adsrFromProp(mv(md::amp_decay)));
        snap[kSnapAmpSus]      .store(juce::jlimit(0.0f, 1.0f, mv(md::amp_sustain)));
        // Filter Cutoff: proportion-space modulation — snap stores ACTUAL Hz
        // converted from the proportion, so the UI's setModulatedActual goes
        // through the slider's setSkewFactorFromMidPoint(640) via valueToProportionOfLength
        // and the arc matches the visual knob by construction.
        snap[kSnapFilterCutoff].store(cutoffFromProp(mv(md::filter_cutoff)));
        snap[kSnapFilterRes]   .store(juce::jlimit(0.0f, 1.0f, mv(md::filter_resonance)));
        // Filter ADSR times: proportion-space modulation — convert back to actual seconds.
        snap[kSnapFenvAtk]     .store(adsrFromProp(mv(md::fenv_attack)));
        snap[kSnapFenvDec]     .store(adsrFromProp(mv(md::fenv_decay)));
        // fenv.depth, pitch.envDepth, accentDb: voiceParams units (semis or dB) differ from the
        // slider's 0..100 display. Store the DISPLAY value (slider units) so setModulatedActual
        // routes through the slider's valueToProportionOfLength correctly.
        snap[kSnapFenvDepth]   .store(kFenvDepth.value(mv(md::fenv_depth)));     // semitones 0..48
        // pitch.semitones: snap stores BASE + OFFSET (in semitones) so the arc tracks the modulated
        // knob position regardless of where the base sits. Pre-fix stored only the offset, so a
        // negative mod read as ABOVE the needle when base was negative (proportion-space follow-up).
        snap[kSnapPitchSemi]   .store(modParams.pitchSemitones + mv(md::pitch_semitones) * kPitchSemi.width());
        // Insert mod snapshots store ACTUAL slider values (per
        // the active algo's slot range / skew) so the UI can run
        // them through `slider.valueToProportionOfLength` via
        // setModulatedActual. Same reasoning as filter cutoff:
        // the slider's log-skew (setSkewFactorFromMidPoint(sqrt(min·max)))
        // is NOT the same curve as the storage-space norm-to-actual
        // (lo · (max/lo)^norm), so a raw normalised snapshot would
        // disagree with the visual needle position. Converting to
        // actual + delegating proportion lookup to the slider
        // guarantees agreement regardless of slot skew (Linear,
        // Log, or IntStep) and regardless of which algorithm is
        // active.
        const int algForSnap = (int) modParams.insertAlgo;
        snap[kSnapInsP1].store(mu_ui::normToActual(mv(md::insert_p1), algForSnap, 0));
        snap[kSnapInsP2].store(mu_ui::normToActual(mv(md::insert_p2), algForSnap, 1));
        snap[kSnapInsP3].store(mu_ui::normToActual(mv(md::insert_p3), algForSnap, 2));
        snap[kSnapInsP4].store(mu_ui::normToActual(mv(md::insert_p4), algForSnap, 3));
        // new destinations — sliders now match voiceParams units (Step 0),
        // so snapshots store the raw value and setModulatedActual routes through the
        // slider's valueToProportionOfLength directly.
        snap[kSnapPitchEnvDep] .store(kPitchEnvDepth.value(mv(md::pitch_envDepth)));  // semitones 0..24
        snap[kSnapAmpLvl]      .store(kAmpLevel.value(mv(md::amp_level)));            // dB -60..+6
        snap[kSnapAccent]      .store(kAccent.value(mv(md::accentDb)));               // dB 0..12
        // filter.lowCut: proportion-space modulation → actual Hz for setModulatedActual.
        snap[kSnapFilterLowCut].store(lowCutFromProp(mv(md::filter_lowCut)));
        // T5 follow-up — pitch.octave: modParamValues holds the modulation offset in SEMITONES (write-back
        // sums it with pitch.semitones into pitchMod). To show the arc on the pitchOctave knob (range -4..+4
        // octaves, linear), store base octave value + offset/12. UI uses setModulatedActual.
        snap[kSnapPitchOctave] .store(modParams.pitchOctave + mv(md::pitch_octave) * kPitchOctave.width());
        // Mixer strip: the actual pan (-1..+1) and send (0..1) values.
        snap[kSnapPan]         .store(modPan);
        snap[kSnapSendEff]     .store(modEff);
        snap[kSnapSendDly]     .store(modDly);
        snap[kSnapSendRev]     .store(modRev);
        // Euclid pattern destinations:
        //   hits/rotate/insSt: proportion-space mod (modParamValues already holds 0..1
        //     slider proportion). snap stores the proportion directly — UI uses
        //     setModulatedNorm.
        //   prePad/postPad/insLen: proportions of the knob's current range; snap
        //     stores the actual step value — UI uses setModulatedActual and maps it
        //     onto that range.
        auto prop = [](float v) { return juce::jlimit(0.0f, 1.0f, v); };
        auto act  = [](float v, int max) { return juce::jlimit(0.0f, 1.0f, v) * (float) max; };
        snap[kSnapEucAHits]    .store(prop(mv(md::euclid_a_hits)));
        snap[kSnapEucARotate]  .store(prop(mv(md::euclid_a_rotate)));
        snap[kSnapEucAPrePad]  .store(act(mv(md::euclid_a_prePad),  padMaxA.prePad));
        snap[kSnapEucAPostPad] .store(act(mv(md::euclid_a_postPad), padMaxA.postPad));
        snap[kSnapEucAInsSt]   .store(prop(mv(md::euclid_a_insSt)));
        snap[kSnapEucAInsLen]  .store(act(mv(md::euclid_a_insLen),  padMaxA.insertLength));
        snap[kSnapEucBHits]    .store(prop(mv(md::euclid_b_hits)));
        snap[kSnapEucBRotate]  .store(prop(mv(md::euclid_b_rotate)));
        snap[kSnapEucBPrePad]  .store(act(mv(md::euclid_b_prePad),  padMaxB.prePad));
        snap[kSnapEucBPostPad] .store(act(mv(md::euclid_b_postPad), padMaxB.postPad));
        snap[kSnapEucBInsSt]   .store(prop(mv(md::euclid_b_insSt)));
        snap[kSnapEucBInsLen]  .store(act(mv(md::euclid_b_insLen),  padMaxB.insertLength));
        snap[kSnapEucCHits]    .store(prop(mv(md::euclid_c_hits)));
        snap[kSnapEucCRotate]  .store(prop(mv(md::euclid_c_rotate)));
        snap[kSnapEucCPrePad]  .store(act(mv(md::euclid_c_prePad),  padMaxC.prePad));
        snap[kSnapEucCPostPad] .store(act(mv(md::euclid_c_postPad), padMaxC.postPad));
        snap[kSnapEucCInsSt]   .store(prop(mv(md::euclid_c_insSt)));
        snap[kSnapEucCInsLen]  .store(act(mv(md::euclid_c_insLen),  padMaxC.insertLength));
    }
}

// Phase 5 — write the modulated values back: the voice params for this block, and the euclid
// overrides (whole steps) that drive the pattern recompute.
void PluginProcessor::writeBackModulation(int r, const Rhythm& rhythm, VoiceParams& modParams)
{
    using namespace mu_clid::mod_skew;
    const auto padMaxA = rhythm.genA.padKnobMaxima();
    const auto padMaxB = rhythm.genB.padKnobMaxima();
    const auto padMaxC = rhythm.genC.padKnobMaxima();
    // Write modulated values back, clamping to safe ranges. Proportion-space
    // destinations convert prop → actual via the shared inverse-skew
    // helpers in ModulationSkew.h (adsrFromProp / lowCutFromProp / cutoffFromProp).
    modParams.ampEnvAtk      = juce::jmax(0.001f, adsrFromProp(mv(md::amp_attack)));
    modParams.ampEnvDec      = juce::jmax(0.001f, adsrFromProp(mv(md::amp_decay)));
    modParams.ampEnvSus      = kSustain.value(mv(md::amp_sustain));
    modParams.filterCutoff   = juce::jlimit(20.0f, 20000.0f, cutoffFromProp(mv(md::filter_cutoff)));
    modParams.filterRes      = kResonance.value(mv(md::filter_resonance));
    modParams.filterEnvAtk   = juce::jmax(0.001f, adsrFromProp(mv(md::fenv_attack)));
    modParams.filterEnvDec   = juce::jmax(0.001f, adsrFromProp(mv(md::fenv_decay)));
    modParams.filterEnvDepth = kFenvDepth.value(mv(md::fenv_depth));
    modParams.filterLowCutHz = lowCutFromProp(mv(md::filter_lowCut));
    // single pitch destination, no more octave×12 + fine/100 stacking.
    // Offsets in proportion of each knob's range → semitones (octave knob ±3 oct = 72 st).
    modParams.pitchMod       = juce::jlimit(-48.0f, 48.0f,
                                             mv(md::pitch_octave) * kPitchOctave.width() * 12.0f
                                           + mv(md::pitch_semitones) * kPitchSemi.width());
    // Stage 36: insert mod write-back to the 4 generic slots.
    // Values stay normalised 0..1; per-algo de-normalisation
    // happens inside each InsertAlgorithm::process via the config
    // table. No algorithm-specific branching needed.
    modParams.insertParam[0] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p1));
    modParams.insertParam[1] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p2));
    modParams.insertParam[2] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p3));
    modParams.insertParam[3] = juce::jlimit(0.0f, 1.0f, mv(md::insert_p4));
    // new destinations write-back
    modParams.pitchEnvDepth  = kPitchEnvDepth.value(mv(md::pitch_envDepth));
    modParams.ampLevel       = kAmpLevel.value(mv(md::amp_level));
    modParams.accentDb       = kAccent.value(mv(md::accentDb));

    // Stage A: write modulated euclid values back to the per-rhythm
    // overrides snapshot. hits/rotate/insSt are proportions of the current step
    // count; prePad/postPad/insLen are proportions of their knobs' current ranges.
    // Both convert back to whole steps.
    auto modPropToSteps = [&](int slot, int steps) {
        return juce::roundToInt(juce::jlimit(0.0f, 1.0f, mv(slot)) * (float) steps);
    };
    const int stepsA_wb = juce::jmax(1, rhythm.genA.steps);
    const int stepsB_wb = juce::jmax(1, rhythm.genB.steps);
    const int stepsC_wb = juce::jmax(1, rhythm.genC.steps);
    lastEuclidOverrides[r].a.hits         = juce::jlimit(0, stepsA_wb,        modPropToSteps(md::euclid_a_hits,  stepsA_wb));
    lastEuclidOverrides[r].a.rotate       = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps(md::euclid_a_rotate, stepsA_wb - 1));
    lastEuclidOverrides[r].a.prePad       = modPropToSteps(md::euclid_a_prePad, padMaxA.prePad);
    lastEuclidOverrides[r].a.postPad      = modPropToSteps(md::euclid_a_postPad, padMaxA.postPad);
    lastEuclidOverrides[r].a.insertStart  = juce::jlimit(0, stepsA_wb - 1,    modPropToSteps(md::euclid_a_insSt, stepsA_wb - 1));
    lastEuclidOverrides[r].a.insertLength = modPropToSteps(md::euclid_a_insLen, padMaxA.insertLength);
    lastEuclidOverrides[r].b.hits         = juce::jlimit(0, stepsB_wb,        modPropToSteps(md::euclid_b_hits,  stepsB_wb));
    lastEuclidOverrides[r].b.rotate       = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps(md::euclid_b_rotate, stepsB_wb - 1));
    lastEuclidOverrides[r].b.prePad       = modPropToSteps(md::euclid_b_prePad, padMaxB.prePad);
    lastEuclidOverrides[r].b.postPad      = modPropToSteps(md::euclid_b_postPad, padMaxB.postPad);
    lastEuclidOverrides[r].b.insertStart  = juce::jlimit(0, stepsB_wb - 1,    modPropToSteps(md::euclid_b_insSt, stepsB_wb - 1));
    lastEuclidOverrides[r].b.insertLength = modPropToSteps(md::euclid_b_insLen, padMaxB.insertLength);
    lastEuclidOverrides[r].c.hits         = juce::jlimit(0, stepsC_wb,        modPropToSteps(md::euclid_c_hits,  stepsC_wb));
    lastEuclidOverrides[r].c.rotate       = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps(md::euclid_c_rotate, stepsC_wb - 1));
    lastEuclidOverrides[r].c.prePad       = modPropToSteps(md::euclid_c_prePad, padMaxC.prePad);
    lastEuclidOverrides[r].c.postPad      = modPropToSteps(md::euclid_c_postPad, padMaxC.postPad);
    lastEuclidOverrides[r].c.insertStart  = juce::jlimit(0, stepsC_wb - 1,    modPropToSteps(md::euclid_c_insSt, stepsC_wb - 1));
    lastEuclidOverrides[r].c.insertLength = modPropToSteps(md::euclid_c_insLen, padMaxC.insertLength);
}

void PluginProcessor::applyRhythmModulation(int r, double beatPos)
{
    Rhythm& rhythm = sequencer.getRhythm(r);
    // Snapshot voiceParams under voiceParamsLock so a concurrent
    // message-thread apply (syncRhythmParam / forceSyncRhythmFromAPVTS)
    // can't interleave a torn write. Held for ~struct-copy time only.
    VoiceParams modParams;
    {
        mu_core::spinLock(rhythm.voiceParamsLock);
        modParams = rhythm.voiceParams;
        mu_core::spinUnlock(rhythm.voiceParamsLock);
    }

    // gate the modulation pass on "matrix has assignments now, OR had
    // assignments last block". The first half is the normal case. The second half
    // runs one final pass on the block AFTER assignment removal so the write-back
    // re-seeds lastEuclidOverrides[r] to base values; Stage B's change-detection
    // then recomputes the safe pattern back to base. Without that transition pass,
    // a never-modulated rhythm pays no per-block cost, but a rhythm whose last
    // assignment was just removed would leave lastEuclidOverrides stuck on the old
    // modulated values, freezing the pattern.
    const bool matrixHasAssignments = !rhythm.modulationMatrix.getAssignments().empty();
    const bool runModulationPass    = matrixHasAssignments || prevMatrixHadAssignments[r];
    prevMatrixHadAssignments[r]     = matrixHasAssignments;

    if (runModulationPass)
    {
        if (mu_core::trySpinLock(rhythm.modLock))
        {
            seedModulation(r, rhythm, modParams);                                      // phase 1
            rhythm.modulationMatrix.process(rhythm.controlSequences, beatPos, modParamValues);   // phase 2
            const StripMod stripMod = applyStripModulation(r);                         // phase 3
            mu_core::spinUnlock(rhythm.modLock);
            publishModSnapshot(r, rhythm, modParams, stripMod);                        // phase 4
            writeBackModulation(r, rhythm, modParams);                                 // phase 5
        }
    }

    // No modulation this block: the mixer uses the strip's own pan / sends.
    if (! runModulationPass)
    {
        auto& strip = mixerEngine.channels[(size_t) r];
        for (auto* m : { &strip.panMod, &strip.sendEffectMod, &strip.sendDelayMod, &strip.sendReverbMod })
            m->store(MixerEngine::ChannelState::kNoMod, std::memory_order_relaxed);
    }

    // Stage B: trigger pattern recompute when integer-rounded overrides changed
    // since last block. Skips recompute on every block where modulation is sub-step
    // (typical case for a slow LFO). tryUpdatePatternFromModulation is non-blocking —
    // a missed try-lock just defers to the next block; prevEuclidOverrides only
    // advances when the recompute actually applied so the change-detection retries.
    if (lastEuclidOverrides[r] != prevEuclidOverrides[r])
    {
        if (sequencer.tryUpdatePatternFromModulation(r, lastEuclidOverrides[r]))
            prevEuclidOverrides[r] = lastEuclidOverrides[r];
    }

    // only override activeParams when the modulation pass actually ran. For an
    // unmodulated rhythm modParams ≡ rhythm.voiceParams ≡ activeParams (already kept in
    // sync by VoiceEngine::applyPendingParams' dirty-flag path), so the call would be
    // pure waste — re-syncing ADSR + filter every block for no change.
    if (runModulationPass && voiceEngines[r])
        voiceEngines[r]->setActiveParams(modParams);
}

#endif // ! MUCLID_LITE_BUILD
