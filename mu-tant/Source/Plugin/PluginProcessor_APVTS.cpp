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

        // Cross-mod character + separate hard-sync toggle.
        layout.add(std::make_unique<AudioParameterInt>   (ParameterID{id("xmod"),  1}, label("X-Mod"),      0, 127, 0));
        layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("xmode"), 1}, label("X-Mod Mode"), StringArray{ "Off", "FM", "AM", "Ring" }, 0));
        layout.add(std::make_unique<AudioParameterBool>  (ParameterID{id("sync"),  1}, label("Osc Sync"), false));

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
        // AudioParameterChoice: ComboBoxAttachment maps choice index 0..N-1 to combo
        // IDs 1..N automatically, so no off-by-one vs AudioParameterInt (where the
        // attachment maps selectedId directly to the int value).
        {
            juce::StringArray filterNames;
            for (int i = 0; mu_audio::kFilterTypeNames[i] != nullptr; ++i)
                filterNames.add(mu_audio::kFilterTypeNames[i]);
            layout.add(std::make_unique<AudioParameterChoice>(ParameterID{id("flt_type"), 1}, label("Filter Type"), filterNames, 0));
        }
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_cut"), 1},  label("Cutoff"), cutoff, 8000.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(cutoffText)));
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_res"), 1},  label("Resonance"), f(0.0f, 0.99f, 0.001f), 0.2f,
                    AudioParameterFloatAttributes().withStringFromValueFunction(resText)));

        // Filter envelope depth — scales how far the filter envelope sweeps from the
        // base cutoff. +1 = full close-to-open sweep; 0 = no effect; -1 = inverted.
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id("flt_env_depth"), 1}, label("Filter Env Depth"), f(-1.0f, 1.0f, 0.01f), 1.0f));

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
        layout.add(std::make_unique<AudioParameterInt>  (ParameterID{c+"scSrc",   1}, n+"SC Src",  0, 8, 0));
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
