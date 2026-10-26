#include "RhythmParams.h"
#include "Modulation/ModulationSkew.h"   // knob ranges shared with modulation (depth = % of range)
#include "Audio/AlgorithmNames.h"        // mu-core: kInsertAlgorithmCount
#include "ValueFormat.h"                 // mu-core: shared value text
#include "Persistence/ParamText.h"       // mu-core: float parameter text in both directions
#include "Sequencer/EuclidParamLayout.h" // mu-core: the Euclid parameter set
#include "Sequencer/GeneratorAliases.h"  // HitGenerator
#include "MuLimits.h"

namespace mu_clid {

// The pad / insert-length modulation ranges must equal the parameters' ranges.
static_assert(mu_clid::mod_skew::kPad.hi == (float) HitGenerator::kMaxPrePad && HitGenerator::kMaxPrePad == HitGenerator::kMaxPostPad, "kPad must match the pad parameters");
static_assert(mu_clid::mod_skew::kInsertLength.hi == (float) HitGenerator::kMaxInsertLength, "kInsertLength must match insLen");

void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int i)
{
    auto addF = [&](const juce::String& id, const juce::String& name, float mn, float mx, float def)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, juce::NormalisableRange<float>(mn, mx), def));
    };
    auto addI = [&](const juce::String& id, const juce::String& name, int mn, int mx, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterInt>(id, name, mn, mx, def));
    };

    // ADSR time params in 0..10 seconds with 0.3 skew. Skew 0.3 puts
    // ~100–200 ms at slider centre so the drum-snap region (1–30 ms) gets
    // generous resolution in the lower third, while pad/ambient values
    // (1–10 s) sit comfortably at the top. Stored value is in seconds
    // directly so host automation lanes show "0.150 s" / "2.5 s" instead
    // of the legacy meaningless 0–100. Format string mirrors the slider's
    // "X ms" below 1 s / "X.XX s" above. Rolled out per envelope to keep
    // each step independently revertable (a → amp, then filter, then pitch).
    auto addAdsrT = [&](const juce::String& id, const juce::String& name, float def)
    {
        const juce::NormalisableRange<float> range(0.0f, 10.0f, 0.0f, 0.3f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            id, name, range, def,
            mu_fmt::floatText(range, [](float v, int) -> juce::String {
                if (v >= 1.0f) return juce::String(v, 2) + " s";
                return juce::String((int)std::round(v * 1000.0f)) + " ms";
            })));
    };

    // Rhythms 0..kMaxAutomatedChannels-1 use full "Rhythm N " names so DAW automation
    // lanes show them clearly. Remaining rhythms use short "RN " names.
    const juce::String p = "r" + juce::String(i) + "_";
    const juce::String n = (i < mu_limits::kMaxAutomatedChannels)
                               ? "Rhythm " + juce::String(i + 1) + " "
                               : "R"       + juce::String(i + 1) + " ";

    // HitGen A / B / C, logic, legato, mono and reset steps: the Euclid set shared with mu-Clid MIDI.
    mu_core::addEuclidParams(layout, p, n);
    // Pitch — octave ±3, semi ±12 (±1 oct), fine ±100 cents (1 cent step).
    // Combined static max = ±4 octaves; clamped at the engine.
    addI(p+"pitchOct",  n+"Pitch Oct",  (int) mu_clid::mod_skew::kPitchOctave.lo, (int) mu_clid::mod_skew::kPitchOctave.hi, 0);
    addI(p+"pitchSemi", n+"Pitch Semi", (int) mu_clid::mod_skew::kPitchSemi.lo,   (int) mu_clid::mod_skew::kPitchSemi.hi,   0);
    addI(p+"pitchFine", n+"Pitch Fine", -100, 100, 0);
    addAdsrT(p+"pEnvAtk", n+"P Env Atk", 0.0f);    // seconds (≈ legacy 0.0 × 0.03)
    addAdsrT(p+"pEnvDec", n+"P Env Dec", 0.03f);   //         (≈ legacy 1.0 × 0.03)
    addF     (p+"pEnvSus", n+"P Env Sus", 0.0f, 100.0f, 0.0f);   // sustain stays 0..100 %
    addAdsrT(p+"pEnvRel", n+"P Env Rel", 0.03f);   //         (≈ legacy 1.0 × 0.03)
    addF(p+"pEnvDep",   n+"P Env Dep",  mu_clid::mod_skew::kPitchEnvDepth.lo, mu_clid::mod_skew::kPitchEnvDepth.hi, 0.0f);
    // Filter
    addI(p+"fltType", n+"Filter Type", 0, 15, 0);  // 0-15: LP12/HP12/BP12/Notch/LP24/HP24/BP24/LP6/Comb+/AP12/Notch24/HP6/Peak/LoShf/HiShf/Comb-
    // log-skewed range. Skew 0.25 puts ~1.3 kHz at slider centre and
    // gives the sub-bass / midrange the resolution they need. Without this,
    // 20–200 Hz lived in ~1% of knob travel.
    const juce::NormalisableRange<float> cutRange(20.0f, 20000.0f, 0.0f, 0.25f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        p+"fltCut", n+"Filter Cut", cutRange, 8000.0f,
        mu_fmt::floatText(cutRange, [](float v, int) -> juce::String {
            if (v < 1000.0f) return juce::String(v, 1) + " Hz";
            return juce::String(v / 1000.0f, 1) + " kHz";
        })));
    addF(p+"fltRes",  n+"Filter Res",   mu_clid::mod_skew::kResonance.lo, mu_clid::mod_skew::kResonance.hi, 0.2f);
    addAdsrT(p+"fEnvAtk", n+"F Env Atk", 0.03f);   // seconds (≈ legacy 1.0 × 0.03)
    addAdsrT(p+"fEnvDec", n+"F Env Dec", 0.09f);   //         (≈ legacy 3.0 × 0.03)
    addF     (p+"fEnvSus", n+"F Env Sus",  0.0f, 100.0f,  0.0f);   // sustain stays 0..100 %
    addAdsrT(p+"fEnvRel", n+"F Env Rel", 0.09f);   //         (≈ legacy 3.0 × 0.03)
    addF(p+"fEnvDep", n+"F Env Dep",  mu_clid::mod_skew::kFenvDepth.lo, mu_clid::mod_skew::kFenvDepth.hi, 0.0f);
    // 4-pole high-pass that sits inline with the main filter. Skewed so most
    // of the knob travel lives in the audible low-end region (0–200 Hz).
    const juce::NormalisableRange<float> loCutRange(0.0f, 1000.0f, 0.0f, 0.35f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        p+"fltLoCut", n+"Filter Low Cut", loCutRange, 0.0f,
        mu_fmt::floatText(loCutRange, [](float v, int) { return mu_fmt::lowCut(v); })));
    // Pre-filter valve saturation depth. 0 = bypass, 1 = full warmth.
    addF(p+"fltDrv", n+"Filter Drive", 0.0f, 1.0f, 0.0f);
    // Amp — level stored in dB (-60..+6), engine converts to gain at read.
    // Default 0 dB = unity gain.
    addF(p+"ampLvl",  n+"Amp Level", mu_clid::mod_skew::kAmpLevel.lo, mu_clid::mod_skew::kAmpLevel.hi, 0.0f);
    addAdsrT(p+"aEnvAtk", n+"A Env Atk", 0.005f);   // seconds
    addAdsrT(p+"aEnvDec", n+"A Env Dec", 0.3f);
    addF(p+"aEnvSus", n+"A Env Sus",  0.0f, 100.0f, 80.0f);   // sustain stays 0..100 %
    addAdsrT(p+"aEnvRel", n+"A Env Rel", 0.5f);
    addF(p+"accentDb",  n+"Accent",     mu_clid::mod_skew::kAccent.lo, mu_clid::mod_skew::kAccent.hi, 0.0f);
    // Insert effect: algo selector + 4 generic Param slots stored as 0..1
    // normalised. Each algorithm's process() reads p.insertParam[N] and
    // converts to the actual value through mu_ui::normToActual using the
    // per-slot range / skew table in Source/Audio/InsertSlotConfig.h. DAW
    // automation lanes show 0..1; the editor UI shows the actual value
    // with the correct unit ("Drive %" / "Bits" / "Mid Hz" / "Note" / ...)
    // based on the active algorithm.
    addI(p+"drvChar", n+"Insert Algo", 0, mu_audio::kInsertAlgorithmCount - 1, 0);
    addF(p+"insP1",   n+"Insert P1",   0.0f, 1.0f, 0.0f);
    addF(p+"insP2",   n+"Insert P2",   0.0f, 1.0f, 0.0f);
    addF(p+"insP3",   n+"Insert P3",   0.0f, 1.0f, 0.0f);
    addF(p+"insP4",   n+"Insert P4",   0.0f, 1.0f, 0.0f);
    // Output mode (0 Sample, 1 MIDI) and the MIDI-output settings used in MIDI mode. Velocity and
    // Accent are percentages; Accent has the meaning of mu-Clid MIDI's Accent knob.
    addI(p+"outMode",    n+"Output Mode",   0, 1,   0);
    addI(p+"midiNote",   n+"MIDI Note",     0, 127, 36);
    addI(p+"midiChan",   n+"MIDI Channel",  1, 16,  1);
    addI(p+"midiVel",    n+"MIDI Velocity", 1, 100, 100);
    addI(p+"midiAccent", n+"MIDI Accent",   0, 100, 0);
}

} // namespace mu_clid
