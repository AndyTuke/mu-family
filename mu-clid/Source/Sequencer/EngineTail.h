#pragma once

#include "Sequencer/RetireTail.h"   // mu-core: what the shared RetireBin holds
#include "Audio/VoiceEngine.h"      // mu-core: the sample voice chain
#include "MuLimits.h"

#include <memory>

// mu-Clid's retire tail (docs/mu-tronic/design.md 2.1.3, E5.5e): the voice engine a hot-swap replaced, still sounding
// its in-flight sample and envelope release in its slot's channel from the shared RetireBin. Natural policy: it ends
// when the engine reports drained (its block-counted drain budget is counted inside the engine, as it always was),
// with the bin's closing 10 ms declick, bounded by the bin's 30 s hard lifetime; an engine with no sample renders
// nothing, so it ends at once. The tail reads nothing but its own engine.
namespace mu_clid
{

class EngineTail final : public mu_core::RetireTail
{
public:
    explicit EngineTail(std::unique_ptr<VoiceEngine> retiring) noexcept : engine(std::move(retiring)) {}

    mu_core::RetirePolicy retirePolicy() const override
    {
        return { mu_core::RetirePolicy::Kind::Natural, 0.0f, (float) mu_limits::kRetireHardCapSeconds, /*closeWithDeclick*/ true };
    }

    // Audio thread, once, at the first tail block: the envelopes enter their release and the drain budget starts.
    void onRetireStart(const mu_core::RenderContext&) override { engine->markRetired(); }

    // Audio thread: the engine adds its tail into its slot's channel, after the slot's live voice; false once drained,
    // or at once for an engine with no sample (it renders nothing and would otherwise hold its entry to the cap).
    bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState&) override
    {
        engine->process(out, n);
        return engine->hasSample() && ! engine->isFullyDrained();
    }

    // Message thread, the audio thread excluded (the host's layer lock): a structural edit moved the tail to slot
    // `slot`, so its noise inserts take that slot's seed, as the slot's live engine does.
    void takeSlot(int slot) noexcept { engine->insertProc.setSlotIndex(slot); }

    // Tests: the engine this tail holds.
    const VoiceEngine& voice() const noexcept { return *engine; }

private:
    std::unique_ptr<VoiceEngine> engine;   // never null
};

} // namespace mu_clid
