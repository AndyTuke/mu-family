// APVTS layout factory split out of PluginProcessor.cpp.
//
// Kept in its own TU (no editor / UI includes) so the test target can compile
// just this + exercise the real createParameterLayout() without dragging the
// whole PluginEditor/UI tree into a console app. Mirrors mu-clid's
// PluginProcessor_APVTS.cpp split.

#include "Plugin/PluginProcessor.h"
#include "Audio/Scales.h"        // kScales (scaleNames)
#include "Audio/AlgorithmNames.h" // mu-core: kFilterTypeNames (shared canonical list)

namespace mu_tant
{

namespace
{
    juce::StringArray rootNames()
    {
        return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    }
    juce::StringArray scaleNames()
    {
        juce::StringArray a;
        for (const auto& s : kScales) a.add(s.name);
        return a;
    }

    // Build the parameter family for a single voice. Called for v0..v7 so each
    // voice gets its own independent osc/xmod/filter/level state in the APVTS.
    void addVoiceParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                        int voice)
    {
        using namespace juce;
        auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };
        auto id = [voice](const char* base) {
            return PluginProcessor::voiceParamId(voice, base);
        };
        auto label = [voice](const char* base) {
            return juce::String("V") + juce::String(voice + 1) + " " + base;
        };

        // Per-oscillator pitch — all integer-stepped. Octave ±3 offset, Semi
        // ±12 (scale-degree, see Scales.h), Fine ±100 cents. Wavetable position
        // is a 0..255 frame index (256-frame Serum/Vital tables).
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_oct"),  1}, label("Osc1 Octave"),   -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_semi"), 1}, label("Osc1 Semi"),     -12, 12, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_fine"), 1}, label("Osc1 Fine"),     -100, 100, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_pos"),  1}, label("Osc1 Position"), 0, 255, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_oct"),  1}, label("Osc2 Octave"),   -3, 3, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_semi"), 1}, label("Osc2 Semi"),     -12, 12, 2));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_fine"), 1}, label("Osc2 Fine"),     -100, 100, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_pos"),  1}, label("Osc2 Position"), 0, 255, 0));

        // Selected wavetable (index into the WavetableBank). Range tracks the
        // factory table count so APVTS + UI dropdown share one source of truth.
        const int maxWt = juce::jmax(0, WavetableBank::factoryTableNames().size() - 1);
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o1_wt"), 1}, label("Osc1 Wavetable"), 0, maxWt, 0));
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("o2_wt"), 1}, label("Osc2 Wavetable"), 0, maxWt, 0));

        // Cross-mod — 2-lane bus model (mu-tant-xmod-design.md).
        //  Lane A (phase/index): one index knob + a mode switch (FM/PM/TZFM) + Sync + Feedback.
        //  Lane B (amplitude):   one bipolar depth knob (AM↔RM morph) + a mode switch (Mult/SSB)
        //                        + a separate SSB shift amount the knob drives in SSB mode.
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("xmod_phaseMode"), 1}, label("X-Mod Phase Mode"), StringArray{ "FM", "PM", "TZFM" }, 1));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("xmod_index"),     1}, label("X-Mod Index"),      f(0.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool>  (ParameterID{id("sync"),           1}, label("Osc Sync"), false));
        layout.add(std::make_unique<AudioParameterBool>  (ParameterID{id("xmod_fdbk"),      1}, label("X-Mod Feedback"), false));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("xmod_ampMode"),   1}, label("X-Mod Amp Mode"), StringArray{ "Mult", "SSB" }, 0));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("xmod_depth"),     1}, label("X-Mod Depth"),     f(-100.0f, 100.0f, 1.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("xmod_ssb"),       1}, label("X-Mod SSB Shift"), f(-2000.0f, 2000.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String { return juce::String((int) std::round(v)) + " Hz"; })));

        // Per-source levels (replace the old osc-balance "mix").
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("o1_lvl"),   1}, label("Osc1 Level"),  f(-60.0f, 6.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("o2_lvl"),   1}, label("Osc2 Level"),  f(-60.0f, 6.0f, 0.1f), -6.0f));
        layout.add(std::make_unique<AudioParameterFloat> (ParameterID{id("noise_lvl"),1}, label("Noise Level"), f(-60.0f, 6.0f, 0.1f), -60.0f));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("noise_type"),1}, label("Noise Type"), StringArray{ "White", "Pink" }, 0));

        // Filter (mu-core) — match mu-clid's filter feel: cutoff 20..20000 Hz
        // log-skewed so the dial centre lands on 640 Hz; resonance 0..0.99.
        // Value formatting lives on the parameter (not the slider) because the
        // JUCE SliderParameterAttachment overwrites the slider's
        // textFromValueFunction with one that calls param.getText — so a
        // slider-side formatter would be clobbered on every voice rebind.
        NormalisableRange<float> cutoff(20.0f, 20000.0f);
        cutoff.setSkewForCentre(640.0f);
        auto cutoffText = [](float v, int) -> String {
            return v < 1000.0f ? String((int) std::round(v))
                               : String(v / 1000.0f, 1);
        };
        auto resText = [](float v, int) -> String { return String((int) std::round(v * 100.0f)); };
        // AudioParameterInt(0..15): stores the algorithm index directly (same
        // normalisation as AudioParameterChoice(16) — both map 0..15 over 0..1).
        // The UI dropdown uses mu_audio::populateFilterTypeDropdown (item ID = index+1)
        // and wires manually (no ComboBoxAttachment) to keep display order independent
        // of the algorithm table order — matching mu-clid's FilterSubsection pattern.
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("flt_type"), 1}, label("Filter Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_cut"), 1},  label("Cutoff"), cutoff, 8000.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(cutoffText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_res"), 1},  label("Resonance"), f(0.0f, 0.99f, 0.001f), 0.2f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(resText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_drv"), 1}, label("Filter Drive"), f(0.0f, 1.0f, 0.01f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String { return juce::String((int)std::round(v * 100.0f)); })));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_lo_cut"), 1}, label("Low Cut"),
                    juce::NormalisableRange<float>(0.0f, 1000.0f, 0.0f, 0.35f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String {
                            if (v <= 0.0f)   return "Off";
                            if (v < 1000.0f) return juce::String((int)std::round(v)) + " Hz";
                            return juce::String(v / 1000.0f, 2) + " kHz";
                        })));

        // Filter 1 envelope depth.
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_env_depth"), 1}, label("Filter Env Depth"), f(-1.0f, 1.0f, 0.01f), 1.0f));

        // Filter 2 — same param set as Filter 1; cutoff defaults to 8 kHz (matching
        // Filter 1) so adding a second filter is audible out of the box, not transparent.
        layout.add(std::make_unique<AudioParameterInt>(ParameterID{id("flt2_type"), 1}, label("Filter2 Type"), 0, 15, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt2_cut"), 1},  label("F2 Cutoff"), cutoff, 8000.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(cutoffText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt2_res"), 1},  label("F2 Resonance"), f(0.0f, 0.99f, 0.001f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(resText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt2_drv"), 1}, label("F2 Drive"), f(0.0f, 1.0f, 0.01f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String { return juce::String((int)std::round(v * 100.0f)); })));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt2_lo_cut"), 1}, label("F2 Low Cut"),
                    juce::NormalisableRange<float>(0.0f, 1000.0f, 0.0f, 0.35f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(
                        [](float v, int) -> juce::String {
                            if (v <= 0.0f)   return "Off";
                            if (v < 1000.0f) return juce::String((int)std::round(v)) + " Hz";
                            return juce::String(v / 1000.0f, 2) + " kHz";
                        })));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt2_env_depth"), 1}, label("F2 Env Depth"), f(-1.0f, 1.0f, 0.01f), 0.0f));

        // Series (true) / Parallel (false) routing for the two filters.
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{id("flt_series"), 1}, label("Filter Series"), true));

        // Pitch envelope depth — semitones added to osc1/osc2 pitch when the pitch
        // envelope is at full value. +24 = 2 oct up; -24 = 2 oct down; 0 = no effect.
        auto penvText = [](float v, int) -> String {
            return String(v, 1) + " st";
        };
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o1_penv_depth"), 1}, label("Osc1 Pitch Env"),
            f(-24.0f, 24.0f, 0.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(penvText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("o2_penv_depth"), 1}, label("Osc2 Pitch Env"),
            f(-24.0f, 24.0f, 0.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(penvText)));

        // Per-voice slot output level — distinct from the mixer fader (engine-level
        // trim before the channel strip; the mixer adds its own per-channel level
        // / pan / mute / solo on top, matching the mu-clid signal flow).
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("level"), 1}, label("Level"), f(-60.0f, 6.0f, 0.1f), -6.0f));

        // Gate Gap — percentage of every gate-envelope region forced to silence
        // at its end, for a cleaner gate. Integer 0..100 %; consumed as /100.
        auto gapText = [](float v, int) -> String { return String((int) std::round(v)) + " %"; };
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("gate_gap"), 1}, label("Gate Gap"), f(0.0f, 100.0f, 1.0f), 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(gapText)));

        // Gater bypass — when on, the gate stage is skipped (raw drone passes,
        // for audition / configuration).
        layout.add(std::make_unique<AudioParameterBool>(ParameterID{id("gate_bypass"), 1}, label("Gate Bypass"), false));

        // Insert effect (shared mu-core InsertProcessor) — same schema as mu-clid:
        // `drvChar` = algorithm 0..(N-1), `insP1..insP4` = generic 0..1 slot params.
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{id("drvChar"), 1}, label("Insert Algo"),
                                                         0, InsertProcessor::kNumInsertAlgos - 1, 0));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP1"), 1}, label("Insert P1"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP2"), 1}, label("Insert P2"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP3"), 1}, label("Insert P3"), f(0.0f, 1.0f, 0.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("insP4"), 1}, label("Insert P4"), f(0.0f, 1.0f, 0.0f), 0.0f));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    // Shared tonal centre.
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"root",  1}, "Root",  rootNames(),  0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{"scale", 1}, "Scale", scaleNames(), 0));

    // Master loop length — 0 = free; 1..16 → 16..256 steps (16 steps = 1 bar, 4 steps/beat).
    // Drives the shared transport MasterLoopSection counter and (future) preset-swap timing.
    // Voices stay free-running — this is a global loop, not coupled to any gate-pattern length.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID{"mstrLoop", 1}, "Master Loop", 0, 16, 0));

    for (int v = 0; v < kMaxVoices; ++v)
        addVoiceParams(layout, v);

    // ── Mixer channel strips (12 params × 8 channels) — matches the shared
    //    mu-core MixerChannel binding prefix `ch{N}_`: level/pan/mute/solo +
    //    FX sends (sendEff/Dly/Rev) + sidechain + output bus. The send/sidechain
    //    params + the global FX rack (below) make the shared MixerOverlay fully
    //    functional; sync flows through ProcessorBase::syncGlobalFxParam.
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = "Voice " + String(i + 1) + " Ch ";
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"lvl",  1}, n+"Level", f(0.0f, 1.0f, 0.001f), 1.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"pan",  1}, n+"Pan",   f(-1.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"mute", 1}, n+"Mute",  false));
        layout.add(std::make_unique<AudioParameterBool> (ParameterID{c+"solo", 1}, n+"Solo",  false));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendEff", 1}, n+"Send Eff", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendDly", 1}, n+"Send Dly", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"sendRev", 1}, n+"Send Rev", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"scSrc",   1}, n+"SC Src",  0, 9, 0));  // 0=off, 1-8=ch0-ch7, 9=ext DAW bus
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAmt",   1}, n+"SC Amount", f(0.0f, 1.0f, 0.001f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scAtk",   1}, n+"SC Attack", f(1.0f, 500.0f, 0.1f), 5.0f));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{c+"scRel",   1}, n+"SC Release", f(10.0f, 2000.0f, 1.0f), 100.0f));
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"outBus",  1}, n+"Output Bus", 0, 8, 0));
    }

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    // Declares eff_/dly_/rev_/echo_/ret_/mstr_/mst_ins* — the IDs the shared
    // MixerOverlay/FXRow/DelayRow bind to. Synced to fxChain/mixerEngine via
    // ProcessorBase::syncGlobalFxParam (listener-driven).
    mu_mixfx::addGlobalFxParams(layout);

    return layout;
}

} // namespace mu_tant
