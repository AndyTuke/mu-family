#include "Persistence/ArpParams.h"
#include "Persistence/LayerParamIds.h"
#include "Sequencer/ArpVoiceRunner.h"         // ArpAccent::kMaxSteps
#include "Audio/ToniVoice.h"                  // kSawPosition
#include "Audio/InsertProcessor.h"            // mu-core: InsertProcessor::kNumInsertAlgos
#include "Audio/Wavetable/WavetableBank.h"    // mu-core: the factory table names
#include "Persistence/ParamText.h"            // mu-core: float parameter text in both directions

namespace mu_toni
{

void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot)
{
    using namespace juce;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    // A time range (seconds) skewed so short attacks/decays get resolution.
    auto tf = []
    {
        NormalisableRange<float> r(0.001f, 4.0f, 0.001f);
        r.setSkewForCentre(0.2f);
        return r;
    };
    NormalisableRange<float> cutR(20.0f, 18000.0f, 1.0f);
    cutR.setSkewForCentre(640.0f);   // the family filter's centre, as mu-Clid and mu-Tant

    NormalisableRange<float> portaR(0.0f, 500.0f, 1.0f);
    portaR.setSkewForCentre(80.0f);   // glide times are mostly short

    const String v = layerParamPrefix(slot);
    auto pid = [&](const char* s) { return ParameterID{ v + s, 1 }; };
    auto lbl = [&](const char* s) { return "V" + String(slot + 1) + " " + s; };

    // Arp
    layout.add(std::make_unique<AudioParameterInt>  (pid("scale"), lbl("Scale"), 0, 11, 1));      // Minor
    layout.add(std::make_unique<AudioParameterInt>  (pid("root"),  lbl("Root"),  0, 11, 0, mu_fmt::intUnit(0, 11, mu_fmt::Unit::Note)));      // C
    layout.add(std::make_unique<AudioParameterInt>  (pid("roct"),  lbl("Root Octave"), 0, 8, 4));
    layout.add(std::make_unique<AudioParameterInt>  (pid("chord"), lbl("Chord"), 0, 34, 3));      // Minor
    layout.add(std::make_unique<AudioParameterInt>  (pid("inv"),   lbl("Inversion"), -4, 4, 0));
    layout.add(std::make_unique<AudioParameterInt>  (pid("octs"),  lbl("Octaves"), 1, 4, 2));
    layout.add(std::make_unique<AudioParameterFloat>(pid("dir"),   lbl("Direction"), f(-100.0f, 100.0f, 1.0f), 100.0f,
                mu_fmt::floatText(f(-100.0f, 100.0f, 1.0f), [](float d, int) -> juce::String
                { return d >= 99.5f ? "Up" : d <= -99.5f ? "Down" : std::abs(d) < 0.5f ? "Up-Down" : mu_fmt::signedNumber(d) + " %"; })));
    layout.add(std::make_unique<AudioParameterInt>  (pid("rate"),  lbl("Rate"), 0, 11, 6));       // 1/16
    layout.add(std::make_unique<AudioParameterFloat>(pid("gate"),  lbl("Gate Length"), f(1.0f, 100.0f, 1.0f), 50.0f, mu_fmt::floatUnit(f(1.0f, 100.0f, 1.0f), mu_fmt::Unit::Percent100)));
    layout.add(std::make_unique<AudioParameterBool> (pid("leg"),   lbl("Legato"), false));
    layout.add(std::make_unique<AudioParameterFloat>(pid("porta"), lbl("Portamento"), portaR, 0.0f, mu_fmt::floatUnit(portaR, mu_fmt::Unit::Ms)));
    layout.add(std::make_unique<AudioParameterBool> (pid("snap"),  lbl("Diatonic Snap"), false));
    layout.add(std::make_unique<AudioParameterInt>  (pid("trig"),  lbl("Trigger"), 0, 1, 0));     // 0=Loop
    // Accent: a repeating on/off pattern over the arp's steps (bit i = step i+1 accented),
    // its length, and how much an accented step lifts (louder + a brighter filter).
    layout.add(std::make_unique<AudioParameterFloat>(pid("acc"),    lbl("Accent"), f(0.0f, 100.0f, 1.0f), 0.0f, mu_fmt::floatUnit(f(0.0f, 100.0f, 1.0f), mu_fmt::Unit::Percent100)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("accLen"), lbl("Pattern Steps"), 1, ArpAccent::kMaxSteps, 4));
    layout.add(std::make_unique<AudioParameterInt>  (pid("accPat"), lbl("Accent Pattern"), 0, (1 << ArpAccent::kMaxSteps) - 1, 1));
    // Step pattern: bit i = step i+1 is On (clear = Off, a rest). Default all On, so layers saved
    // before this existed play every step as before.
    layout.add(std::make_unique<AudioParameterInt>  (pid("stepMask"), lbl("Step Mask"), 0, (1 << ArpAccent::kMaxSteps) - 1, (1 << ArpAccent::kMaxSteps) - 1));

    // Oscillators — mu-Tant's wavetable oscs: a table from the shared bank + its scan
    // position (frame 0..255), defaulting to Basic Shapes' saw.
    const int maxWt = juce::jmax(0, mu_wavetable::WavetableBank::factoryTableNames().size() - 1);
    layout.add(std::make_unique<AudioParameterInt>  (pid("o1_wt"),  lbl("Osc1 Wavetable"), 0, maxWt, 0));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o1o"),    lbl("Osc1 Octave"), -3, 3, 0, mu_fmt::intUnit(-3, 3, mu_fmt::Unit::Octaves)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("o1f"),    lbl("Osc1 Fine"), f(-100.0f, 100.0f, 1.0f), 0.0f, mu_fmt::floatUnit(f(-100.0f, 100.0f, 1.0f), mu_fmt::Unit::Cents)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o1_pos"), lbl("Osc1 Position"), 0, 255, kSawPosition));
    layout.add(std::make_unique<AudioParameterFloat>(pid("o1l"),    lbl("Osc1 Level"), f(-60.0f, 6.0f, 0.1f), 0.0f, mu_fmt::floatUnit(f(-60.0f, 6.0f, 0.1f), mu_fmt::Unit::Db)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o2_wt"),  lbl("Osc2 Wavetable"), 0, maxWt, 0));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o2o"),    lbl("Osc2 Octave"), -3, 3, 0, mu_fmt::intUnit(-3, 3, mu_fmt::Unit::Octaves)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o2s"),    lbl("Osc2 Semi"), -12, 12, 0, mu_fmt::intUnit(-12, 12, mu_fmt::Unit::Semitones)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("o2f"),    lbl("Osc2 Fine"), f(-100.0f, 100.0f, 1.0f), 7.0f, mu_fmt::floatUnit(f(-100.0f, 100.0f, 1.0f), mu_fmt::Unit::Cents)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("o2_pos"), lbl("Osc2 Position"), 0, 255, kSawPosition));
    layout.add(std::make_unique<AudioParameterFloat>(pid("o2l"),    lbl("Osc2 Level"), f(-60.0f, 6.0f, 0.1f), -3.0f, mu_fmt::floatUnit(f(-60.0f, 6.0f, 0.1f), mu_fmt::Unit::Db)));

    // Cross-mod — mu-Tant's 2-lane model (same ids + ranges): Lane A index + mode + Sync +
    // Feedback; Lane B bipolar depth + mode, with the SSB shift in Hz.
    layout.add(std::make_unique<AudioParameterChoice>(pid("xmod_phaseMode"), lbl("X-Mod Phase Mode"), StringArray{ "FM", "PM", "TZFM" }, 1));
    layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_index"),     lbl("X-Mod Index"), f(0.0f, 100.0f, 1.0f), 0.0f, mu_fmt::floatUnit(f(0.0f, 100.0f, 1.0f), mu_fmt::Unit::Percent100)));
    layout.add(std::make_unique<AudioParameterBool>  (pid("sync"),           lbl("Osc Sync"), false));
    layout.add(std::make_unique<AudioParameterBool>  (pid("xmod_fdbk"),      lbl("X-Mod Feedback"), false));
    layout.add(std::make_unique<AudioParameterChoice>(pid("xmod_ampMode"),   lbl("X-Mod Amp Mode"), StringArray{ "AM", "RM", "SSB" }, 0));
    layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_depth"),     lbl("X-Mod Depth"), f(-100.0f, 100.0f, 1.0f), 0.0f,
                mu_fmt::floatUnit(f(-100.0f, 100.0f, 1.0f), mu_fmt::Unit::Percent100)));
    layout.add(std::make_unique<AudioParameterFloat> (pid("xmod_ssb"),       lbl("X-Mod SSB Shift"), f(-2000.0f, 2000.0f, 1.0f), 0.0f,
                mu_fmt::floatText(f(-2000.0f, 2000.0f, 1.0f),
                    [](float val, int) -> juce::String { return juce::String((int) std::round(val)) + " Hz"; })));

    // Noise / filter
    layout.add(std::make_unique<AudioParameterFloat>(pid("noise"), lbl("Noise Level"), f(-60.0f, 0.0f, 0.1f), -60.0f, mu_fmt::floatUnit(f(-60.0f, 0.0f, 0.1f), mu_fmt::Unit::Db)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("ntype"), lbl("Noise Type"), 0, 1, 0));    // 0=White
    layout.add(std::make_unique<AudioParameterInt>  (pid("ft"),  lbl("Filter Type"), 0, 15, 0));
    layout.add(std::make_unique<AudioParameterFloat>(pid("cut"), lbl("Cutoff"), cutR, 3000.0f, mu_fmt::floatUnit(cutR, mu_fmt::Unit::Hz)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("res"), lbl("Resonance"), f(0.0f, 0.99f, 0.001f), 0.3f, mu_fmt::floatUnit(f(0.0f, 0.99f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("drv"), lbl("Drive"), f(0.0f, 1.0f, 0.001f), 0.0f, mu_fmt::floatUnit(f(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    { NormalisableRange<float> loR(0.0f, 1000.0f, 1.0f); loR.setSkewForCentre(200.0f);
      layout.add(std::make_unique<AudioParameterFloat>(pid("locut"), lbl("Low Cut"), loR, 0.0f,
                mu_fmt::floatText(loR, [](float v, int) { return mu_fmt::lowCut(v); }))); }

    // Amp / Filter / Pitch ADSR
    layout.add(std::make_unique<AudioParameterFloat>(pid("aeA"), lbl("Amp Attack"),  tf(), 0.004f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("aeD"), lbl("Amp Decay"),   tf(), 0.15f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("aeS"), lbl("Amp Sustain"), f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::floatUnit(f(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("aeR"), lbl("Amp Release"), tf(), 0.15f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("aeL"), lbl("Amp Level"),   f(-60.0f, 6.0f, 0.1f), 0.0f, mu_fmt::floatUnit(f(-60.0f, 6.0f, 0.1f), mu_fmt::Unit::Db)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("feA"), lbl("Filter Attack"),  tf(), 0.004f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("feD"), lbl("Filter Decay"),   tf(), 0.20f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("feS"), lbl("Filter Sustain"), f(0.0f, 1.0f, 0.001f), 0.0f, mu_fmt::floatUnit(f(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("feR"), lbl("Filter Release"), tf(), 0.20f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("feDep"), lbl("Filter Env Depth"), f(-1.0f, 1.0f, 0.001f), 0.3f, mu_fmt::floatUnit(f(-1.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("peA"), lbl("Pitch Attack"),  tf(), 0.004f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("peD"), lbl("Pitch Decay"),   tf(), 0.10f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("peS"), lbl("Pitch Sustain"), f(0.0f, 1.0f, 0.001f), 0.0f, mu_fmt::floatUnit(f(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("peR"), lbl("Pitch Release"), tf(), 0.10f, mu_fmt::floatUnit(tf(), mu_fmt::Unit::Seconds)));
    layout.add(std::make_unique<AudioParameterFloat>(pid("peDep"), lbl("Pitch Env Depth"), f(-24.0f, 24.0f, 0.1f), 0.0f, mu_fmt::floatUnit(f(-24.0f, 24.0f, 0.1f), mu_fmt::Unit::Semitones)));
    layout.add(std::make_unique<AudioParameterInt>  (pid("ptgt"),  lbl("Pitch Env Target"), 0, 1, 1));   // 1=Osc 2 only

    // Insert effect (shared mu-core InsertProcessor) — same schema as mu-clid/mu-tant.
    layout.add(std::make_unique<AudioParameterInt>  (pid("drvChar"), lbl("Insert Algo"), 0, InsertProcessor::kNumInsertAlgos - 1, 0));
    layout.add(std::make_unique<AudioParameterFloat>(pid("insP1"), lbl("Insert P1"), f(0.0f, 1.0f, 0.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(pid("insP2"), lbl("Insert P2"), f(0.0f, 1.0f, 0.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(pid("insP3"), lbl("Insert P3"), f(0.0f, 1.0f, 0.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(pid("insP4"), lbl("Insert P4"), f(0.0f, 1.0f, 0.0f), 0.0f));
}

void addArpGlobalParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace juce;
    StringArray chans { "Omni" };
    for (int c = 1; c <= 16; ++c) chans.add(String(c));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ kMidiInChannelParamId, 1 }, "MIDI In Channel", chans, 0));
}

} // namespace mu_toni
