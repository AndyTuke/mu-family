#include "Sequencer/Phrase.h"

#include "Modulation/LaneModulation.h"   // mu-core: mu_mod::resolveLane
#include "Modulation/StripMods.h"        // mu-core: paramIdFor

#include <algorithm>
#include <cstring>

// The Phrase layer's half of the render contract (docs/mu-tronic/design.md 2.1).
namespace mu_tilate
{

namespace
{
    // The ppi row a modulation target drives, found from its table parameter name.
    int rowFor(const char* param)
    {
        for (int k = 0; k < ppi::COUNT; ++k)
            if (std::strcmp(ppi::suffix[k], param) == 0) return k;
        jassertfalse;   // a table row names a parameter that does not exist
        return 0;
    }
}

void Phrase::setData(std::shared_ptr<const PhraseData> d)
{
    const double beats = d != nullptr ? d->timeline.lengthBeats() : -1.0;
    {
        mu_core::ScopedSpinLock<CopyableSpinLock> lock(dataLock);
        data.swap(d);
    }
    loopLengthBeats.store(beats, std::memory_order_relaxed);
    // `d` now holds the old data and is released here, on the message thread, outside the lock.
}

std::shared_ptr<const PhraseData> Phrase::getData() const
{
    mu_core::ScopedSpinLock<CopyableSpinLock> lock(dataLock);
    return data;
}

void Phrase::prepare(double sampleRate, int blockSize)
{
    // The stretch window is a per-layer choice made here (Short for drums, Default for tonal loops).
    playback->prepare(sampleRate, juce::jmax(16, blockSize), pp[ppi::window].load(1.0f) < 0.5f);
    insert->prepare(sampleRate, blockSize);
    tailScratch.setSize(2, juce::jmax(1, blockSize));
    tailScratch.clear();
    parked = true;
    lastData = nullptr;
    lastResolved.reset();   // a prepare is a fresh start: no held modulated values
}

void Phrase::bindParams(juce::AudioProcessorValueTreeState& apvts, int slot)
{
    const auto prefix = layerParamPrefix(slot);
    for (int k = 0; k < kNumPhraseParams; ++k)
        pp[(size_t) k] = mu_core::ParamBinding(apvts.getRawParameterValue(prefix + ppi::suffix[k]), this);

    // The modulation inputs are the same atomics, in target-table order.
    for (int k = 0; k < kNumModDests; ++k)
    {
        const auto id = mu_mod::paramIdFor(prefix, slot, kModDestTable[k].param);
        modDestIds[(size_t) k]    = kModDestTable[k].id;
        modDestRanges[(size_t) k] = apvts.getParameterRange(id);
        modDestAtoms[(size_t) k]  = apvts.getRawParameterValue(id);
        modParamValues[kModDestTable[k].id] = 0.0f;   // pre-size the map: the audio thread only overwrites
    }
    lastResolved.reset();   // values held belong to the slot bound before
}

void Phrase::readParams(double modBeat) noexcept
{
    auto g = [this](int row) { return pp[(size_t) row].load(0.0f); };

    // Resolve this layer's modulation matrix over its control sequences -> out[] (param units). A block that misses
    // the matrix lock holds the last resolved values, so no target snaps back to its knob for a block.
    float out[kNumModDests];
    const bool resolved = mu_mod::resolveLane(this, modBeat, kNumModDests, modDestIds.data(), modDestAtoms.data(),
                                              modDestRanges.data(), modParamValues, out);
    lastResolved.apply(resolved, out);
    for (int k = 0; k < kNumModDests; ++k)
        modLive.v[(size_t) k].store(out[k], std::memory_order_relaxed);

    settings.mode      = (TempoMode) juce::jlimit(0, 2, (int) g(ppi::mode));   // not modulated
    settings.loopTempo = (double) g(ppi::ltempo);                              // not modulated
    settings.holdMs    = out[D_hold];
    settings.strength  = out[D_strength];
    settings.fadeInMs  = out[D_fadeIn];
    settings.fadeOutMs = out[D_fadeOut];
    settings.gain      = out[D_level];

    insertCfg.insertAlgo     = (int) g(ppi::drvChar);
    insertCfg.insertParam[0] = out[D_insP1];
    insertCfg.insertParam[1] = out[D_insP2];
    insertCfg.insertParam[2] = out[D_insP3];
    insertCfg.insertParam[3] = out[D_insP4];
}

void Phrase::renderBlock(const PhraseData* d, PhraseClock clock, juce::AudioBuffer<float>& buf, int n) noexcept
{
    PhraseLoop loop;
    if (d != nullptr)
    {
        loop.audio = &d->audio;
        loop.fileSampleRate = d->fileSampleRate;
        loop.slices = &d->slices;
        loop.timeline = &d->timeline;
    }
    playback->render(loop, settings, clock, buf, n);
    insert->process(buf, n, buf.getNumChannels(), insertCfg);
}

void Phrase::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    // A slot that has just gone live (or was prepared since) starts from silence.
    if (parked) { playback->hardReset(); lastData = nullptr; lastResolved.reset(); parked = false; }
    if (stopRequested) { playback->reset(); stopRequested = false; }

    readParams(ctx.startBeat);

    // The loop data for this block, or nothing: a miss keeps time without sound, the next block carries on.
    if (! mu_core::trySpinLock(dataLock)) return;
    const PhraseData* d = data.get();
    if (d != lastData) { playback->hardReset(); lastData = d; }   // the voices point into the old data: drop them

    PhraseClock clock;
    clock.playing        = ctx.playing != 0;
    clock.startBeat      = ctx.startBeat;
    clock.beatsPerSample = ctx.beatsPerSample;
    clock.bpm            = ctx.bpm;
    clock.sampleRate     = ctx.sampleRate;
    renderBlock(d, clock, out, n);
    mu_core::spinUnlock(dataLock);
}

// The tail of a layer that left its slot: the scheduler with the transport stopped (the sounding slice fades through
// its cut fade), faded by the bin, then the insert with the configuration it last had. The layer is out of its slot, so
// nobody swaps its data, and no parameter is read.
bool Phrase::renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs)
{
    juce::AudioBuffer<float> aside;
    if (rs.exclusive == 0)
    {
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), juce::jmin(2, out.getNumChannels()),
                               juce::jmin(n, tailScratch.getNumSamples()));
        aside.clear();
    }
    auto& buf = rs.exclusive != 0 ? out : aside;
    const int frames = juce::jmin(n, buf.getNumSamples());

    PhraseClock idle;
    idle.sampleRate = ctx.sampleRate;
    renderBlock(data.get(), idle, buf, frames);
    mu_core::applyLinearFade(buf, frames, rs);

    if (rs.exclusive == 0)
        for (int c = 0; c < buf.getNumChannels(); ++c) out.addFrom(c, 0, buf, c, 0, frames);
    return true;   // the bin ends the fade when its ramp is spent
}

} // namespace mu_tilate
