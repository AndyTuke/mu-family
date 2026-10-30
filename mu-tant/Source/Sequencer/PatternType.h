#pragma once

#include "Sequencer/Pattern.h"
#include "Sequencer/LayerTypeInfo.h"       // mu-core: mu_core::LayerTypeInfo
#include "Sequencer/LayerServices.h"       // mu-core: what the type shares between its layers
#include "Persistence/PatternParams.h"     // addLayerParams, addPatternGlobalParams
#include "Audio/NoiseSeed.h"               // mu-core: makeNoiseSalt
#include "Audio/Wavetable/WavetableBank.h" // mu-core: the oscillators' tables

#include <array>
#include <memory>

// The Pattern layer type's description for a host that offers several layer types (docs/mu-tronic/design.md 2.5),
// as mu-Toni's ArpType.h and mu-On's TrackTypes.h do. Header-only on purpose: only a host that registers the type
// compiles it. Message-thread data only: it describes and constructs, it never dispatches (rendering stays virtual
// on Layer). What the host adds is its adapter (a PatternHost over the services below), so the host also supplies
// createServices and attachServices, and a Pattern subclass through `create` if it saves more than the type does.
namespace mu_tant
{

// What every Pattern of one processor shares (design 2.4): the wavetable bank, the lock that guards its appends, and
// the slots' engines. Built when the processor's first Pattern is added and kept until the processor is destroyed,
// after every Pattern and tail. mu-Tant's own processor keeps the same members itself and does not use this.
class PatternServices : public mu_core::LayerServices
{
public:
    PatternServices() { bank.loadFactoryBank(); }

    // Message thread (and prepareToPlay): the engines get the rate and block size the processor was prepared with.
    // rt-ok-fn: message thread; the audio thread reads the bank through BankTryLock, never the blocking engineLock/bankLock
    void prepare(double sampleRate, int blockSize) override
    {
        const juce::ScopedLock sl(engineLock);
        rate = sampleRate;
        block = blockSize;
        for (auto& pair : engines)
            for (auto& e : pair)
                if (e != nullptr) e->prepare(rate, block);
    }

    // Message thread, once for each new Pattern of `slot`, before it binds: the slot's engine for that tenant. A slot has
    // a pool of two engines (built and prepared on the slot's first use) and each new Pattern takes the one its
    // predecessor did not, so a Pattern replaced by another type fades out through its own engine while a successor put
    // back into the slot inside the fade renders through the other: two layers never advance one engine's phase and
    // filters in the same block. Nothing is allocated on the audio thread; a third Pattern inside one 25 ms fade would
    // reuse the engine of the oldest tail (not reachable from the UI).
    // rt-ok-fn: message thread; the audio thread reads the bank through BankTryLock, never the blocking engineLock/bankLock
    VoiceEngine* engineForNewLayer(int slot)
    {
        if (slot < 0 || slot >= mu_limits::kMaxLayers) return nullptr;
        const juce::ScopedLock sl(engineLock);
        auto& pair = engines[(size_t) slot];
        for (auto& e : pair)
            if (e == nullptr)
            {
                e = std::make_unique<VoiceEngine>();
                e->setBank(&bank);
                e->setSlotIndex(slot, noiseSalt);
                if (rate > 0.0) e->prepare(rate, block);
            }
        current[(size_t) slot] ^= 1;
        return pair[(size_t) current[(size_t) slot]].get();
    }

    // Message thread, a Published host's swap (audio excluded): the two slots' engine pools move with their Patterns, so a
    // moved Pattern keeps its own engine (its oscillator phase and filters continue) and the pool's alternation, which
    // is per slot, stays true for the Pattern a later edit puts in either slot. A tail in a swapped bin row moves with it.
    // rt-ok-fn: message thread; the audio thread reads the bank through BankTryLock, never the blocking engineLock/bankLock
    void slotsSwapped(int a, int b) override
    {
        if (a < 0 || b < 0 || a >= mu_limits::kMaxLayers || b >= mu_limits::kMaxLayers || a == b) return;
        const juce::ScopedLock sl(engineLock);
        std::swap(engines[(size_t) a], engines[(size_t) b]);
        std::swap(current[(size_t) a], current[(size_t) b]);
        // Each engine takes its new slot's noise seed, as one made for that slot does, so two Patterns never share a sequence.
        for (const int slot : { a, b })
            for (auto& e : engines[(size_t) slot])
                if (e != nullptr) e->setSlotIndex(slot, noiseSalt);
    }

    // Message thread, the slot's current engine (the newest Pattern's; null before the first): what a Pattern's
    // bindParams is handed. Allocates nothing and takes no lock, as a bindParams under layersLock requires.
    VoiceEngine* findEngine(int slot) const noexcept
    {
        return slot >= 0 && slot < mu_limits::kMaxLayers ? engines[(size_t) slot][(size_t) current[(size_t) slot]].get() : nullptr;
    }

    // The bank index of a wavetable file: found by path, else decoded OFF the lock (file read, WAV decode and FFT
    // mip build are the slow part) and appended under it for microseconds, so an import never silences the render
    // for the decode. -1 when unreadable. Message thread.
    int bankIndexFor(const juce::File& file)
    {
        assertMessageThread();
        return bank.indexForFile(file, bankLock);
    }

    // A saved wavetable path as a bank index (-1: none, or the file is gone). Lock-free lookup first: a staged load
    // pre-loaded the table, so the commit at the boundary never takes bankLock (a try-lock miss silences the render
    // for that call); only a path nobody pre-loaded takes the locked disk path. Message thread.
    int resolveWavetable(const juce::String& path)
    {
        if (path.isEmpty()) return -1;
        assertMessageThread();   // the lock-free lookup is sound only because every append is made on this thread
        if (const int found = bank.findByPath(path); found >= 0) return found;
        const juce::File f(path);
        return f.existsAsFile() ? bankIndexFor(f) : -1;
    }

    mu_wavetable::WavetableBank bank;
    juce::CriticalSection       bankLock;   // guards appends to `bank` against the audio render, which try-locks it

private:
    // Debug check (headless tests with no MessageManager skip it): the bank's lookups and appends are one thread.
    static void assertMessageThread() noexcept
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            jassert(mm->isThisTheMessageThread());
    }

    const juce::int64 noiseSalt = mu_core::makeNoiseSalt();   // per processor: slots of two instances do not share noise
    juce::CriticalSection engineLock;                          // the table of engines (creation, prepare)
    std::array<std::array<std::unique_ptr<VoiceEngine>, 2>, (size_t) mu_limits::kMaxLayers> engines;
    std::array<int, (size_t) mu_limits::kMaxLayers> current {};   // which of the slot's two engines the newest Pattern uses
    double rate = 0.0;
    int    block = 0;
};

// A fresh Pattern for `slot`, not yet bound or prepared. The insert's delay lines are allocated here, off the audio
// thread and outside any lock; the per-slot seed keeps layers from sharing a noise sequence.
inline std::unique_ptr<Layer> createPattern(int slot, mu_core::LayerTypeId /*type*/)
{
    auto p = std::make_unique<Pattern>();
    p->insert->setSlotIndex(slot);
    return p;
}

inline mu_core::LayerTypeInfo patternLayerTypeInfo() noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = mu_core::LayerTypeId::Pattern;
    info.name = "Pattern";
    info.group = "mu-Tant";
    info.paramPrefixLetter = 'v';   // kept from mu-Tant's first release: the ids are users' automation targets
    info.presetExtension = ".muPattern";
    info.contentFolderName = "muTant";   // the layer presets are mu-Tant's own, shared with it, not copied
    info.presetFolder = "Voices";        // the folder mu-Tant keeps its .muPattern files in (design-naming: a rename is on hold)
    info.presetTag = kPatternPresetTag;
    info.descriptor = "Wavetable drone, gate pattern";
    info.discInitial = "P";
    info.addLayerParams = &addLayerParams;
    info.addGlobalParams = &addPatternGlobalParams;   // the tonal centre (root, scale) every Pattern reads
    info.create = &createPattern;
    return info;
}

} // namespace mu_tant
