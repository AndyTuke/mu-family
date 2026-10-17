// APVTS layout factory split out of PluginProcessor.cpp.
//
// Kept in its own TU (no editor / UI includes) so the test target can compile
// just this + exercise the real createParameterLayout() without dragging the
// whole PluginEditor/UI tree into a console app. Mirrors mu-clid's
// PluginProcessor_APVTS.cpp split. The Pattern layer type's own rows live in
// Persistence/PatternParams.cpp.

#include "Plugin/PluginProcessor.h"
#include "Persistence/PatternParams.h"   // the Pattern type's rows (addLayerParams, the tonal centre)

namespace mu_tant
{

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Shared tonal centre (root, scale).
    addPatternGlobalParams(layout);

    // Master loop length — 0 = free; 1..16 → 16..256 steps (16 steps = 1 bar, 4 steps/beat).
    // Drives the shared transport MasterLoopSection counter and (future) preset-swap timing.
    // Voices stay free-running — this is a global loop, not coupled to any gate-pattern length.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID{"mstrLoop", 1}, "Master Loop", 0, 16, 0));

    for (int v = 0; v < mu_limits::kMaxLayers; ++v)
        addLayerParams(layout, v);

    // ── Mixer channel strips (12 params × 8 channels) — matches the shared
    //    mu-core MixerChannel binding prefix `ch{N}_`: level/pan/mute/solo +
    //    FX sends (sendEff/Dly/Rev) + sidechain + output bus. The send/sidechain
    //    params + the global FX rack (below) make the shared MixerOverlay fully
    //    functional; sync flows through ProcessorBase::syncGlobalFxParam.
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        const String c = "ch" + String(i) + "_";
        const String n = "Voice " + String(i + 1) + " Ch ";
        mu_mixfx::addChannelStripParams(layout, c, n);
    }

    // ── Shared global FX rack + returns + master (mu-core) ────────────────────
    // Declares eff_/dly_/rev_/echo_/ret_/mstr_/mst_ins* — the IDs the shared
    // MixerOverlay/FXRow/DelayRow bind to. Synced to fxChain/mixerEngine via
    // ProcessorBase::syncGlobalFxParam (listener-driven).
    mu_mixfx::addGlobalFxParams(layout);

    return layout;
}

} // namespace mu_tant
