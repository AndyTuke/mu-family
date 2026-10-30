#include "Plugin/PluginProcessor.h"
#include "Plugin/SwapBoundary.h"   // mu-tant: the hot-swap boundary rule (the host's policy)
#include "Plugin/MidiRaw.h"        // mu-core: note tests on raw MIDI bytes (no allocation)
#include "License/ProductLicensing.h"   // mu-core: ProcessorBase::initLicensing (licensed products only)
#include "Plugin/HostTransport.h"            // mu-core: shared host-playhead reader

namespace mu_tant
{

PluginProcessor::PluginProcessor()
    : ProcessorBase(BusesProperties()
                        .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput("Output",    juce::AudioChannelSet::stereo(), true),
                    createParameterLayout(),
                    juce::Identifier("MuTantState"))
{
    // All eight Patterns exist for the processor's lifetime (capacity is fixed, only the count is
    // runtime), so a preset can fill a slot above the live count before the count grows.
    keepSpareLayers = true;
    for (int i = 0; i < mu_limits::kMaxLayers; ++i)
    {
        layers[(size_t) i] = createLayer(i);
        layers[(size_t) i]->colourIndex = i;   // each voice starts on its own palette colour
        jassert(dynamic_cast<Pattern*>(layers[(size_t) i].get()) != nullptr);   // the one type check; layerAs only casts
    }
    storeLayerCount(1, std::memory_order_relaxed);   // through the packed layout word, as every count store

    initAppSettings("muTant");   // settings file + saved UI size / MIDI clock (ProcessorBase)

    // Restore persisted MIDI Note mode (0 = Free, 1 = Note). Seed the audio-thread
    // gate so a fresh open in Note mode starts silent (closed) until a note arrives.
    midiNoteMode.store(appSettings->getIntValue("midiNoteMode", 0), std::memory_order_relaxed);
    lastNoteMode = midiNoteMode.load(std::memory_order_relaxed);
    noteGateGain = (lastNoteMode == 1) ? 0.0f : 1.0f;

    // Offline licence + online activation (shared; after initAppSettings for getContentDir).
    initLicensing({ kLicenseProductId, kLicenseFilename, kLicensePublicKey, "mutant.activation", kCheckoutUrl });

    // MIDI program-change preset maps (Ch 1-8 → per-voice .muPattern, Ch 9 →
    // full .muTant preset). The scan/drain machinery + the editor panels live in
    // mu-core; we only point the maps at the plugin's settings dir + load them.
    // Mirrors mu-clid.
    {
        const auto settingsDir = appSettings->getFile().getParentDirectory();
        midiPresetMap    .setStorageFile(settingsDir.getChildFile("muTant_midiPresets.json"));
        midiFullPresetMap.setStorageFile(settingsDir.getChildFile("muTant_midiFullPresets.json"));
        midiPresetMap    .load();
        midiFullPresetMap.load();
    }

    // The slot engines: one per slot, handed to the Pattern in that slot (PatternHost::getPatternEngine).
    bank.loadFactoryBank();      // multi-table, mip-mapped factory wavetable bank
    for (int v = 0; v < mu_limits::kMaxLayers; ++v)
    {
        voices[(size_t) v] = std::make_unique<VoiceEngine>();
        voices[(size_t) v]->setBank(&bank);
        voices[(size_t) v]->setSlotIndex(v, noiseSeedSalt);
    }

    cacheParamPointers();        // resolve the boundary rule's APVTS atomics once
    initVoiceState();            // the voice layout every preset / session save and load uses
    refreshAllPitchQuantFlags(); // seed the stepped-pitch flags (default state has no modulators)

    // The shared layer driver: a voice a count-reducing preset drops fades in place past the live count
    // (RenderPastCount, the old renderCount rule); the channel messages of a block the driver does not render
    // reach the layers on the next one (each Pattern keeps its own held keys, which the old shared stack heard
    // in every block); a regrow keeps an armed fade armed (pin P1, until its signed golden update).
    setRetireCountPolicy(mu_core::RetireBin::CountPolicy::RenderPastCount);
    setCarrySkippedMidi(true);
    setRegrowCutsInPlaceTails(false);
    // Every slot's Pattern binds now that the slot engines exist, and starts in the restored Note mode with an
    // input state of its own (it hears every block from the first).
    for (int v = 0; v < mu_limits::kMaxLayers; ++v)
    {
        auto& p = getPattern(v);
        p.bindParams(apvts, v);
        p.initNoteMode(midiNoteMode.load(std::memory_order_relaxed));
        p.markInputSeeded();
    }

    layerSwapper.setAppliers([this](juce::ValueTree& t) { applyFullPresetTree(t); },
                             [this](int v, juce::ValueTree& t)
                             {
                                 applyVoicePresetTree(v, t);
                                 if (onLayerPresetCommitted) onLayerPresetCommitted(v);
                             });
    startFxParamSync();     // mixer / FX params → mixerEngine + fxChain (ProcessorBase)
    loadStartupDefault();   // restore a saved _default preset (skipped by render mode)
}

PluginProcessor::~PluginProcessor()
{
    prepareForDestruction();   // first: no deferred-work tick may reach this half-destroyed object
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resetSwapPlayState();   // no processBlock is running yet: loads apply at once, not staged
    currentSampleRate.store(sampleRate, std::memory_order_relaxed);
    currentBlockSize.store(samplesPerBlock, std::memory_order_relaxed);
    for (auto& v : voices)
        if (v) v->prepare(sampleRate, samplesPerBlock);

    mixerEngine.prepare(sampleRate, samplesPerBlock);
    fxChain.prepare(sampleRate, samplesPerBlock);   // shared Effect/Delay/Reverb rack

    // The shared driver's buffers and every slot's Pattern (its insert), spares included; the retire bin is
    // flushed (a fade's ramp length was derived from the old rate).
    prepareLayerRender(sampleRate, samplesPerBlock);
}

void PluginProcessor::cacheParamPointers()
{
    mstrLoopPtr   = apvts.getRawParameterValue("mstrLoop");                   // master-loop length
    swapTimingPtr = apvts.getRawParameterValue(mu_hotswap::kSwapTimingParamId);  // hot-swap timing (mu_hotswap::SwapTiming)
}

// The shared layer driver (docs/mu-tronic/design.md 2.1.1): program changes queued, the shell's Note-mode keys
// scanned (the final-mix gate's input, every host block), the transport by the family rule wrapped into the
// 64-beat pattern space, then one try-lock of the layers, the Patterns (and the in-place retire tails) and the
// mixer; the Note-mode gate on the final mix (decision A3: a shell post-stage, so the FX returns are gated as
// they always were); the boundary flagging from the per-slot loop lengths the driver cached. Real-time: nothing
// here allocates or waits; a block the driver cannot render (an edit in flight, a missed lock) is silent while
// the transport, the keys and the boundary detection still advance.
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;   // the shell's own (N7): the final-mix gate runs outside the driver
    const int numSamples = buffer.getNumSamples();

    // MIDI program-change → preset load. Enqueue matching PCs (Ch 1-8 per-voice, Ch 9 full) into the
    // lock-free FIFO; runDeferredWork drains them. Before the render, so PCs aren't dropped during an edit.
    queueMidiProgramChanges(midiMessages);
    scanNoteMode(midiMessages);

    const auto t = resolveBlockTransport(midiMessages, numSamples, Pattern::kBeatSpace);
    renderAndMix(buffer, midiMessages, t);
    applyNoteModeGate(buffer, numSamples);

    std::array<double, mu_limits::kMaxLayers> beats {};
    for (int s = 0; s < mu_limits::kMaxLayers; ++s) beats[(size_t) s] = layerLoopBeats(s);
    flagPatternSwapBoundaries(t.playing, t.startBeat, t.blockBeats, beats.data());
}

// The hot-swap boundary check: a staged preset commits when its reference loop wraps (or on the
// playing→stopped edge). The block's beat span starts at the block start and runs the RAW (pre-wrap) block
// length, so the loop-index test holds for pattern lengths that don't divide 64. The reference loop depends
// on the swap timing (`mstr_swap`) + the master loop:
//   • master-loop length in beats = mstrLoop steps / 4 (4 steps/beat); 0 = off.
//   • per-voice swap: OnMasterLoop (+ a loop set) → master loop; else the voice's own gate-pattern boundary
//     (so a swap can't hang when no master loop exists).
//   • full preset: master loop when one is defined, else voice 0's gate boundary.
void PluginProcessor::flagPatternSwapBoundaries(bool playing, double startBeat, double blockBeats,
                                                const double* patternBeats) noexcept
{
    const int    mlSteps         = mstrLoopPtr ? juce::jmin((int) mstrLoopPtr->load() * 16, 256) : 0;
    const double masterLoopBeats = mlSteps > 0 ? (double) mlSteps / 4.0 : 0.0;
    const float  swapRaw         = swapTimingPtr != nullptr ? swapTimingPtr->load(std::memory_order_relaxed) : 0.0f;
    const auto   swapTiming      = mu_hotswap::swapTimingFromIndex(std::isfinite(swapRaw) ? (int) std::lround(swapRaw) : 0);

    const int numActive = numLayers.load(std::memory_order_relaxed);
    flagSwapBoundaries(playing, startBeat, blockBeats,
                       [&](int slot) noexcept
                       {
                           return swapLoopBeats(slot, numActive, swapTiming, masterLoopBeats, patternBeats);
                       });
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = captureFullPreset();
    saveLiveState(state);   // the host session also keeps the Performance (path, current item, mode)
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Validate the root tag before applying (mirrors mu-on / mu-toni): a corrupt or
    // foreign chunk that still parses as XML must NOT replace our APVTS root, or the
    // params detach. Only apply a tree tagged as our own state type.
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            cancelAllStagedSwaps();                    // nothing staged before the restore may land on top of it
            applyFullPresetTree(toVoiceState(tree));   // host state restore — always immediate
            restoreLiveState(tree);                    // after the preset: it names the item that is playing
        }
    notifyHostStateRestored();
}

// ── Dynamic voice management ─────────────────────────────────────────────────
// A fresh default Pattern, linked to this processor. Built before a structural edit locks, so the
// edit under the lock only moves pointers.
std::unique_ptr<Layer> PluginProcessor::createLayer(int slot)
{
    auto p = std::make_unique<Pattern>();
    p->host.ptr = this;
    // The insert allocates its delay lines here, off the audio thread and outside any lock; the
    // per-slot noise seed makes layers not share one sequence.
    p->insert->setSlotIndex(slot);
    p->prepare(currentSampleRate.load(std::memory_order_relaxed), currentBlockSize.load(std::memory_order_relaxed));
    p->bindParams(apvts, slot);
    p->initNoteMode(midiNoteMode.load(std::memory_order_relaxed));   // its held keys come from seedInputState
    return p;
}

// Slots whose Pattern changed lose the previous tenant's filter, oscillator-modulation and insert memory (the
// slot's engine stays and is reset and re-seeded; the insert travels with its Pattern, so it is re-seeded for the
// new slot and cleared), any fade-out tail and the live-arc snapshot; the mixer strip and its ducking envelope are
// reset by the edit itself. Under layersLock with processing suspended: allocation-free resets only.
void PluginProcessor::layersMoved(int firstIndex, int lastIndex)
{
    for (int d = juce::jmax(0, firstIndex); d <= juce::jmin(lastIndex, mu_limits::kMaxLayers - 1); ++d)
    {
        if (voices[(size_t) d] != nullptr) { voices[(size_t) d]->setSlotIndex(d, noiseSeedSalt); voices[(size_t) d]->reset(); }
        auto& ins = *getPattern(d).insert;   // the Pattern now in slot d brings its own insert along
        ins.setSlotIndex(d);
        ins.reset();
        // The slot's in-place fade is cut (the bin's lock nests inside layersLock: order layersLock, binMutex), the
        // live-arc values belong to the slot left, and a Pattern built after the constructor takes the held keys
        // the others heard.
        retireBin.cutSlot(d);
        auto& pat = getPattern(d);
        pat.resetSlotState();
        if (! pat.hasInputState()) seedInputState(d);
    }
}

// Every Pattern that existed from the processor's first block has heard every block's keys, so any one of
// them holds what today's single held-note stack holds; the new one copies it. Under layersLock: no block
// reads or writes either Pattern meanwhile, and the lock orders the audio thread's last writes before this.
void PluginProcessor::seedInputState(int slot) noexcept
{
    for (int s = 0; s < mu_limits::kMaxLayers; ++s)
        if (s != slot && layers[(size_t) s] != nullptr && getPattern(s).hasInputState())
        {
            getPattern(slot).takeInputStateFrom(getPattern(s));
            return;
        }
    jassertfalse;   // unreachable: the constructor's Patterns are seeded and every later one is seeded here
}

// The bank index for a wavetable file: found by path, else decoded OFF bankLock and appended under it for
// microseconds (the decode under the lock would silence the audio render for the whole import).
int PluginProcessor::bankIndexFor(const juce::File& file)
{
    return bank.indexForFile(file, bankLock);
}

void PluginProcessor::loadUserWavetable(int voice, int oscIndex, const juce::File& file)
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers || ! file.existsAsFile()) return;
    const int idx = bankIndexFor(file);
    if (idx < 0) { if (onLoadError) onLoadError("Could not load wavetable \"" + file.getFileName() + "\""); return; }
    const juce::String path = file.getFullPathName();
    if (oscIndex == 0) { getPattern(voice).osc1UserPath = path; getPattern(voice).osc1UserIndex.store(idx); }
    else             { getPattern(voice).osc2UserPath = path; getPattern(voice).osc2UserIndex.store(idx); }
}

void PluginProcessor::clearUserWavetable(int voice, int oscIndex)
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers) return;
    if (oscIndex == 0) { getPattern(voice).osc1UserPath.clear(); getPattern(voice).osc1UserIndex.store(-1); }
    else             { getPattern(voice).osc2UserPath.clear(); getPattern(voice).osc2UserIndex.store(-1); }
}

juce::String PluginProcessor::userWavetablePath(int voice, int oscIndex) const
{
    if (voice < 0 || voice >= mu_limits::kMaxLayers) return {};
    return (oscIndex == 0) ? getPattern(voice).osc1UserPath : getPattern(voice).osc2UserPath;
}

bool PluginProcessor::userWavetableMissing(int voice, int oscIndex) const
{
    const auto p = userWavetablePath(voice, oscIndex);
    return p.isNotEmpty() && ! juce::File(p).existsAsFile();
}

// ── Settings ─────────────────────────────────────────────────────────────────
void PluginProcessor::setMidiNoteMode(int mode)
{
    midiNoteMode.store(mode, std::memory_order_relaxed);
    for (int v = 0; v < mu_limits::kMaxLayers; ++v)
        if (layers[(size_t) v] != nullptr) getPattern(v).setNoteMode(mode);   // each layer resets its keys on its next block
    if (appSettings != nullptr) { appSettings->setValue("midiNoteMode", mode); appSettings->saveIfNeeded(); }
}

// ── Note mode: the final-mix gate (decision A3; the pitch track is each Pattern's) ──
// Audio-thread-only: scanNoteMode + applyNoteModeGate both run within processBlock
// on one thread, so the held-note stack + ramp state need no synchronisation.

void PluginProcessor::scanNoteMode(const juce::MidiBuffer& midi)
{
    const int mode = midiNoteMode.load(std::memory_order_relaxed);

    // A Free↔Note switch resets the held stack + gate so neither mode inherits stale
    // state (e.g. Free wouldn't get stuck silent, Note wouldn't open from a phantom note).
    if (mode != lastNoteMode)
    {
        gateNotes.clear();
        noteGateGain = (mode == 1) ? 0.0f : 1.0f;
        lastNoteMode = mode;
    }

    if (mode != 1)
        return;

    // The keys of the whole host block, rendered or not (a block an edit silences still opens or closes the
    // gate): a Note On pushes, a Note Off removes (HeldNoteStack, the same rule each Pattern applies).
    for (const auto meta : midi)
    {
        if (mu_midi::isNoteOn (meta))       gateNotes.push(mu_midi::noteNumber (meta));
        else if (mu_midi::isNoteOff (meta)) gateNotes.remove(mu_midi::noteNumber (meta));
    }
}

void PluginProcessor::applyNoteModeGate(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (midiNoteMode.load(std::memory_order_relaxed) != 1)
        return;

    // Linear amplitude ramp toward 1 (a note held) or 0 (all released), reaching the
    // target over ~8 ms so the held→silent edge fades instead of clicking.
    const float target   = gateNotes.numHeld() > 0 ? 1.0f : 0.0f;
    const float fadeSecs  = 0.008f;
    const float gainStep = 1.0f / (float) juce::jmax(1.0, fadeSecs * currentSampleRate.load(std::memory_order_relaxed));

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    float  g = noteGateGain;
    for (int i = 0; i < numSamples; ++i)
    {
        g += juce::jlimit(-gainStep, gainStep, target - g);
        l[i] *= g;
        if (r != nullptr) r[i] *= g;
    }
    noteGateGain = g;
}

// ── Presets ──────────────────────────────────────────────────────────────────
} // namespace mu_tant

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mu_tant::PluginProcessor();
}
