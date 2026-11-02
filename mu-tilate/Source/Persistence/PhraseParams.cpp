#include "Persistence/PhraseParams.h"

#include "Persistence/InsertParams.h"   // mu-core: mu_mixfx::addInsertParams

namespace mu_tilate
{

void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot)
{
    const auto prefix = layerParamPrefix(slot);
    auto id = [&](int row) { return juce::ParameterID { prefix + ppi::suffix[row], 1 }; };
    auto addF = [&](int row, const char* name, float lo, float hi, float def, float skewMid = 0.0f)
    {
        juce::NormalisableRange<float> r(lo, hi);
        if (skewMid > 0.0f) r.setSkewForCentre(skewMid);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id(row), name, r, def));
    };
    auto addStepped = [&](int row, const char* name, float lo, float hi, float def, float step)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(id(row), name, juce::NormalisableRange<float>(lo, hi, step), def));
    };
    auto addC = [&](int row, const char* name, const juce::StringArray& choices, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(id(row), name, choices, def));
    };

    // Playback: the tempo mode and the loop's tempo, the stretch's hold and strength, the slice fades and the level.
    addC(ppi::mode,     "Tempo Match",    tempoModeNames(), 1);
    addF(ppi::tmix,     "Pitch / Stretch", 0.0f, 1.0f, 1.0f);
    addF(ppi::ltempo,   "Loop Tempo",     20.0f, 300.0f, 120.0f);
    addF(ppi::hold,     "Transient Hold", 0.0f, 100.0f, 10.0f, 15.0f);
    addF(ppi::strength, "Strength",       0.0f, 1.0f, 1.0f);
    addF(ppi::fadeIn,   "Fade In",        0.0f, 50.0f, 0.5f, 5.0f);
    addF(ppi::fadeOut,  "Fade Out",       0.0f, 50.0f, 0.5f, 5.0f);
    addF(ppi::gain,     "Level",          0.0f, 2.0f, 1.0f);

    // Timeline: the grid and the stretch window (the length is saved with the timeline, not a parameter).
    addC(ppi::grid,     "Grid",           gridNames(), 3);
    addC(ppi::window,   "Stretch Window", windowNames(), 1);

    // Detection: the method and its controls, and its controls.
    addC(ppi::dmethod,  "Detect Method",  detectMethodNames(), 0);
    addF(ppi::dsens,    "Sensitivity",    0.0f, 1.0f, 0.5f);
    addF(ppi::dgap,     "Min Gap",        5.0f, 500.0f, 50.0f, 60.0f);
    addF(ppi::dpre,     "Pre-roll",       0.0f, 20.0f, 2.0f);
    addF(ppi::dlow,     "Low Cut",        20.0f, 2000.0f, 20.0f, 200.0f);
    addF(ppi::dhigh,    "High Cut",       1000.0f, 20000.0f, 20000.0f, 6000.0f);

    // The layer's filter: every slice uses it, moved by its own cutoff and resonance offsets.
    addC(ppi::fType,    "Filter Type",    filterTypeNames(), 0);
    addF(ppi::fCut,     "Filter Cutoff",  20.0f, 20000.0f, 20000.0f, 1000.0f);
    addF(ppi::fRes,     "Filter Resonance", 0.0f, 1.0f, 0.0f);

    // The layer's pitch, pan and envelopes: absolute values; a slice only adds its own offsets to them.
    addStepped(ppi::pSemi, "Pitch",           -24.0f, 24.0f, 0.0f, 1.0f);
    addStepped(ppi::pFine, "Fine Tune",       -100.0f, 100.0f, 0.0f, 1.0f);
    addF(ppi::pPan,     "Pan",            -1.0f, 1.0f, 0.0f);
    addF(ppi::ampA,     "Amp Attack",     0.0f, 2000.0f, 0.0f, 100.0f);
    addF(ppi::ampD,     "Amp Decay",      0.0f, 2000.0f, 100.0f, 100.0f);
    addF(ppi::ampS,     "Amp Sustain",    0.0f, 1.0f, 1.0f);
    addF(ppi::ampR,     "Amp Release",    0.0f, 2000.0f, 0.0f, 100.0f);
    addF(ppi::fEnv,     "Filter Envelope Amount", -1.0f, 1.0f, 0.0f);
    addF(ppi::fltA,     "Filter Attack",  0.0f, 2000.0f, 0.0f, 100.0f);
    addF(ppi::fltD,     "Filter Decay",   0.0f, 2000.0f, 100.0f, 100.0f);
    addF(ppi::fltS,     "Filter Sustain", 0.0f, 1.0f, 1.0f);
    addF(ppi::fltR,     "Filter Release", 0.0f, 2000.0f, 0.0f, 100.0f);

    // The layer's insert effect (the shared rows).
    mu_mixfx::addInsertParams(layout, prefix, "Phrase ");
}

} // namespace mu_tilate
