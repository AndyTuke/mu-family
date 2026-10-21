#pragma once

#include "Sequencer/SampleRhythm.h"
#include "Sequencer/RhythmCursor.h"
#include "Sequencer/LayerTypeInfo.h"       // mu-core: mu_core::LayerTypeInfo
#include "Sequencer/LayerServices.h"       // mu-core: what the type shares between its layers
#include "Persistence/RhythmParams.h"      // addLayerParams, kRhythmPresetTag
#include "Persistence/RhythmPresetTree.h"  // readRhythm, loadPresetSample, toRelativeSamplePath
#include "Persistence/LayerState.h"        // mu-core: the composed node's row tag
#include "Modulation/ModulatorSerialise.h"
#include "Audio/SampleLibrary.h"
#include "Audio/SpinLock.h"                // mu-core: spin lock helpers
#include "Audio/VoiceEngine.h"
#include "Audio/MixerEngine.h"
#include "Modulation/ModulationSnapshot.h"
#include "MuLimits.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

// The Rhythm layer type's description for a host that offers several layer types (docs/mu-tronic/design.md 2.5), as
// mu-Toni's ArpType.h, mu-On's TrackTypes.h and mu-Tant's PatternType.h do for their types. Header-only on purpose: only a
// host that registers the type compiles it, so mu-Clid's own build and its recorded sound are untouched. Message-thread
// data except where a member says otherwise.
//
// What a slot host gets here that mu-Clid's processor gives its rhythms: SlotRhythm clocks itself from the render
// context (the step clock and cursor, the hit, the play state, the mixer strip for the modulation pass, the MIDI out),
// follows its parameters (RhythmSlotServices applies a changed r{N}_ row to the layer), saves its sample, and fades out
// when its slot changes type. mu-Clid's own processor keeps doing all of this itself with SampleRhythm.
namespace mu_clid
{

class RhythmSlotServices;

// The Euclid step cap every pattern is built with (the same value as mu-Clid's PluginProcessor::kStepCap).
inline constexpr int kRhythmStepCap = HitGenerator::kMaxSteps;

// A Rhythm in one slot of a host that holds several layer types.
class SlotRhythm : public SampleRhythm
{
public:
    SlotRhythm() { midiScratch.ensureSize(256); }

    // Message thread, before the layer is prepared, bound or published: wires it to the type's services and its slot.
    void attachServices(RhythmSlotServices& s, int slotIndex) noexcept { services = &s; slot = slotIndex; }
    int  getSlot() const noexcept { return slot; }

    // ── mu_core::Layer ───────────────────────────────────────────────────────
    void prepare(double sampleRate, int blockSize) override;
    // The layer reads no parameter on the audio thread (the services apply a changed row to its fields), so binding
    // only learns the slot, which the services and the mixer strip are looked up by.
    void bindParams(juce::AudioProcessorValueTreeState&, int slotIndex) override { slot = slotIndex; }
    // Moved to another slot: the voice's insert takes that slot's noise seed, as a layer created there does.
    void onMoved(int newIndex) override { if (voice != nullptr) voice->insertProc.setSlotIndex(newIndex); }
    void completeSetup(juce::AudioProcessorValueTreeState& apvts, int slotIndex) override;
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;
    // A pattern's pass is the boundary a staged preset waits for; negative until the cursor holds a pattern.
    double loopBeats() const override { return cursor.loopBeats(); }
    const std::atomic<float>* insertGainReduction() const override
    {
        return voice != nullptr ? &voice->insertProc.grReduction : nullptr;
    }

    // ── A type change fades the voice out over the family's one policy (design 2.1.11 point 3) ──
    mu_core::RetirePolicy retirePolicy() const override
    {
        mu_core::RetirePolicy p;
        p.kind = mu_core::RetirePolicy::Kind::Fade;   // fadeMs and maxSeconds stay at the family defaults
        return p;
    }
    void onRetireStart(const mu_core::RenderContext& ctx) override
    {
        // The MIDI note-off the layer still owed goes out now: the layer will not render again.
        int note = 0, channel = 1;
        if (midi.takePending(note, channel))
            ctx.emitMidi(0, (uint8_t) (0x80 | juce::jlimit(0, 15, channel - 1)), (uint8_t) note, 0);
    }
    bool renderRetiring(const mu_core::RenderContext&, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override
    {
        // The voice's own state only: no parameter, no matrix, no trigger. An engine with no sample renders nothing.
        if (voice == nullptr || ! voice->hasSample()) return false;
        const int frames = juce::jmin(n, out.getNumSamples());
        if (rs.exclusive != 0)
        {
            voice->process(out, frames);
            mu_core::applyLinearFade(out, frames, rs);
            return true;
        }
        // The slot's block already holds the new layer (or an earlier tail): render aside, fade, then add.
        // A call is never longer than the prepared size tailScratch was sized to; asserted, and clamped (the fade then
        // runs short for those frames) rather than chunked, since the bin's fade ramp is indexed from the call's first frame.
        jassert(frames <= tailScratch.getNumSamples());
        juce::AudioBuffer<float> aside;
        const int channels = juce::jmin(2, out.getNumChannels());
        aside.setDataToReferTo(tailScratch.getArrayOfWritePointers(), channels, juce::jmin(frames, tailScratch.getNumSamples()));
        aside.clear();
        voice->process(aside, aside.getNumSamples());
        mu_core::applyLinearFade(aside, aside.getNumSamples(), rs);
        for (int c = 0; c < channels; ++c)
            out.addFrom(c, 0, aside, c, 0, aside.getNumSamples());
        return true;
    }

    // ── What the layer saves beyond its r{N}_ rows: the modulators (base) and the sample ──
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;
    // Message thread, a staged preset: its sample is read now, so the commit at the boundary does no file work. This
    // writes `staged` on the live layer from the message thread, which is safe only because applyExtras (its one reader)
    // also runs on the message thread; the audio thread never touches `staged`.
    void preloadStaged(const juce::ValueTree& staged) override;

    // Message thread: every r{N}_ row of the slot onto the layer, then the pattern and the voice's parameters.
    void syncFromParameters(juce::AudioProcessorValueTreeState& apvts, int slotIndex);

    // Message thread: one changed row (by its place in kRhythmParamDefs) applied to the layer. `refresh` rebuilds the
    // pattern and hands the voice its parameters afterwards (false while a bulk change is in flight).
    void applyParameter(int defIndex, float actualValue, bool refresh);

private:
    // Audio thread, a rendered host block's first call: the step clock and cursor, the hit, the play state, and the
    // inputs of the modulation pass that SampleRhythm::render runs next.
    void startBlock(const mu_core::RenderContext& ctx);
    // Audio thread, after the call's voice render: the MIDI the call produced goes out through the context.
    void flushMidi(const mu_core::RenderContext& ctx, int n);

    // What the sample load needs from the host, and the key that tells a staged node's sample from another's.
    RhythmPresetContext presetContext() const;
    static juce::String sampleKey(const juce::ValueTree& node);

    RhythmSlotServices*                    services = nullptr;
    int                                    slot = -1;
    double                                 preparedRate = 0.0;
    int                                    preparedBlock = 0;

    // Audio-thread state: this layer's own step clock (the host has none), and the transport memory of the last block.
    RhythmStepClock clock;
    double          lastBeat = 0.0;
    bool            wasPlaying = false;
    juce::MidiBuffer midiScratch;          // the MIDI engine's events for one call, forwarded through the context
    juce::AudioBuffer<float> tailScratch;  // renderRetiring's aside buffer, sized in prepare

    // A staged preset's sample, read at stage time (message thread only).
    struct StagedSample
    {
        bool valid = false;
        juce::String key;
        std::optional<juce::String> path;   // what loadPresetSample returned: the path to record, or none (leave the voice alone)
        VoiceEngine::DecodedSample sample;
    };
    StagedSample staged;
    bool extrasApplied = false;   // a preset or restored state has been applied: the built-in start no longer applies
};

// What every Rhythm of one processor shares (design 2.4): the sample library (the user's primary folder, the browser's
// preview), the transport flag the circles animate on, and the applying of a changed parameter row to its layer. The
// host adapter derives from it and supplies the few things only the processor knows. A layer library never includes the
// processor, so these arrive as virtuals.
class RhythmSlotServices : public mu_core::LayerServices,
                           public SampleSlotHost,
                           private juce::AudioProcessorParameter::Listener
{
public:
    // Message thread: listens to every r{N}_ row of every slot. The parameters outlive the services (the processor owns
    // the parameter objects; the services are destroyed before them), so the listeners are removed safely in ~.
    explicit RhythmSlotServices(juce::AudioProcessorValueTreeState& apvts)
        : ownerThread(std::this_thread::get_id())
    {
        for (int s = 0; s < mu_limits::kMaxLayers; ++s)
            for (int d = 0; d < kRhythmParamCount; ++d)
            {
                const auto id = "r" + juce::String(s) + "_" + kRhythmParamDefs[d].suffix;
                if (auto* p = apvts.getParameter(id))
                {
                    params[(size_t) s][(size_t) d] = p;
                    byIndex[p->getParameterIndex()] = { s, d, p };
                    p->addListener(this);
                }
            }
    }
    ~RhythmSlotServices() override
    {
        for (auto& row : params)
            for (auto* p : row)
                if (p != nullptr) p->removeListener(this);
    }

    // ── What the host supplies ───────────────────────────────────────────────
    // The Rhythm layer in `slot`, or null when the slot holds another type. Message thread.
    virtual SlotRhythm* rhythmAt(int slot) const noexcept = 0;
    // The mixer strip of `slot` (the modulation pass reads and drives its pan and sends); stable for the processor's life.
    virtual MixerEngine::ChannelState* stripFor(int slot) noexcept = 0;
    // Any thread, allocation-free: asks the message thread for deferredWork.
    virtual void requestWork() noexcept = 0;
    virtual juce::File getSamplesDir() const = 0;
    virtual void reportLoadError(const juce::String& message) = 0;

    // ── Shared state ─────────────────────────────────────────────────────────
    SampleLibrary samples { *this };
    // Written by every Rhythm layer's render (the transport as the last rendered block saw it), read by the UI.
    std::atomic<bool> transportPlaying { false };
    // While set, a changed row is applied to the layer but its pattern and voice are not refreshed: the code that set it
    // finishes with syncFromParameters (the insert algorithm switch writes several rows at once).
    std::atomic<bool> bulkChange { false };

    // ── mu_core::LayerServices ───────────────────────────────────────────────
    void prepare(double sampleRate, int blockSize) override { samples.prepare(blockSize, sampleRate); }
    void mixIntoMaster(juce::AudioBuffer<float>& master, int numSamples) noexcept override
    {
        samples.mixPreviewInto(master, numSamples);
    }
    // A Published host's swap: the sample paths are kept per slot, so they move with the layers (mu-Clid's own host does
    // the same in its layer edits). The voices, and so the audio, are the layers'.
    void slotsSwapped(int a, int b) override { samples.swapPaths(a, b); }

    void deferredWork() override
    {
        // Rows that changed on another thread: each read from the parameter itself, so the newest value wins.
        for (int s = 0; s < mu_limits::kMaxLayers; ++s)
            if (slotDirty[(size_t) s].exchange(false, std::memory_order_acq_rel))
                for (int d = 0; d < kRhythmParamCount; ++d)
                    if (dirty[(size_t) s][(size_t) d].exchange(false, std::memory_order_acq_rel))
                        applyChange(s, d, params[(size_t) s][(size_t) d]->convertFrom0to1(params[(size_t) s][(size_t) d]->getValue()));
    }

    // ── SampleSlotHost: the engines and the settings file the sample library uses ──
    int getSampleSlotCount() const noexcept override { return mu_limits::kMaxLayers; }
    VoiceEngine*       getSampleSlotEngine(int slot) noexcept override
    {
        auto* r = rhythmAt(slot);
        return r != nullptr ? r->voice.get() : nullptr;
    }
    const VoiceEngine* getSampleSlotEngine(int slot) const noexcept override
    {
        auto* r = rhythmAt(slot);
        return r != nullptr ? r->voice.get() : nullptr;
    }

    // Message thread: the changed row of slot `s` (by its place in kRhythmParamDefs) onto the slot's Rhythm, if it holds one.
    void applyChange(int s, int d, float actualValue)
    {
        if (auto* r = rhythmAt(s))
            r->applyParameter(d, actualValue, ! bulkChange.load(std::memory_order_acquire));
    }

private:
    struct Entry { int slot; int def; juce::RangedAudioParameter* param; };

    // Any thread (host automation, a controller): the message thread applies it at once; another thread queues it.
    void parameterValueChanged(int parameterIndex, float newValue) override
    {
        const auto it = byIndex.find(parameterIndex);
        if (it == byIndex.end()) return;
        const auto& e = it->second;
        if (isMessageThread())
        {
            applyChange(e.slot, e.def, e.param->convertFrom0to1(newValue));
            return;
        }
        dirty[(size_t) e.slot][(size_t) e.def].store(true, std::memory_order_release);
        slotDirty[(size_t) e.slot].store(true, std::memory_order_release);   // the row's flag first, then the slot's
        requestWork();
    }
    void parameterGestureChanged(int, bool) override {}

    // With no MessageManager (a headless test) the thread that built the services stands in for the message thread.
    bool isMessageThread() const noexcept
    {
        const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        return mm != nullptr ? mm->isThisTheMessageThread() : std::this_thread::get_id() == ownerThread;
    }

    std::array<std::array<juce::RangedAudioParameter*, (size_t) kRhythmParamCount>, (size_t) mu_limits::kMaxLayers> params {};
    std::unordered_map<int, Entry> byIndex;   // built once; the listener only looks it up
    std::array<std::array<std::atomic<bool>, (size_t) kRhythmParamCount>, (size_t) mu_limits::kMaxLayers> dirty {};
    std::array<std::atomic<bool>, (size_t) mu_limits::kMaxLayers> slotDirty {};
    const std::thread::id ownerThread;
};

// ── SlotRhythm ───────────────────────────────────────────────────────────────

inline void SlotRhythm::prepare(double sampleRate, int blockSize)
{
    SampleRhythm::prepare(sampleRate, blockSize);
    preparedRate  = sampleRate;
    preparedBlock = blockSize;
    tailScratch.setSize(2, juce::jmax(1, blockSize), false, false, true);
}

inline void SlotRhythm::applyParameter(int defIndex, float actualValue, bool refresh)
{
    if (defIndex < 0 || defIndex >= kRhythmParamCount) return;
    bool patternDirty = false, voiceDirty = false;
    // Held per field, as mu-Clid does, so the audio thread's brief seed-copy of the voice parameters slips in between.
    mu_core::spinLock(voiceParamsLock);
    kRhythmParamDefs[defIndex].apply(actualValue, *this, patternDirty, voiceDirty);
    mu_core::spinUnlock(voiceParamsLock);
    if (! refresh) return;
    if (patternDirty) cursor.updatePattern(*this, kRhythmStepCap);
    if (voiceDirty && voice != nullptr) voice->setParams(voiceParams);
}

inline void SlotRhythm::syncFromParameters(juce::AudioProcessorValueTreeState& apvts, int slotIndex)
{
    const juce::String prefix = "r" + juce::String(slotIndex) + "_";
    for (int j = 0; j < kRhythmParamCount; ++j)
        if (auto* raw = apvts.getRawParameterValue(prefix + kRhythmParamDefs[j].suffix))
            applyParameter(j, raw->load(), false);
    cursor.updatePattern(*this, kRhythmStepCap);
    if (voice != nullptr) voice->setParams(voiceParams);
}

inline void SlotRhythm::completeSetup(juce::AudioProcessorValueTreeState& apvts, int slotIndex)
{
    // A layer nothing has been applied to (no preset, no restored state: applyExtras has not run) starts as mu-Clid's
    // first rhythm does: sixteen steps with four hits and no sample. mu-Clid ships no default sample; an app default
    // file (_default.muRhythm) reaches the layer as a node before this, through ProcessorBase::createLayerOfType.
    if (! extrasApplied)
        for (const auto& [suffix, plain] : { std::pair<const char*, float> { "stepsA", 16.0f }, { "hitsA", 4.0f } })
            if (auto* p = apvts.getParameter("r" + juce::String(slotIndex) + "_" + suffix))
                p->setValueNotifyingHost(p->convertTo0to1(plain));
    // The layer's fields meet the slot's rows before it is published.
    syncFromParameters(apvts, slotIndex);
}

inline void SlotRhythm::startBlock(const mu_core::RenderContext& ctx)
{
    block = {};   // each block starts with no hit and no modulation inputs
    const bool playing = ctx.playing != 0;

    // The transport rule mu-Clid's processor applies to every block: a transport that just started, or a real
    // backward jump, restarts the step counter; a dip of less than a step is held where it was.
    const auto resolved = RhythmSequencer::resolveBlockBeat(lastBeat, ctx.startBeat, playing, wasPlaying, 0);
    wasPlaying = playing;
    lastBeat   = resolved.beat;

    bool fired = false;
    if (playing)
    {
        if (resolved.resetWrap) clock.resetWrapDetector();
        const StepFrame frame = clock.advance(resolved.beat);
        const auto hit = cursor.advance(frame);
        clock.endBlock(frame);
        fired = hit.fired;
        if (hit.fired)
        {
            midiScratch.clear();
            takeHit(hit.accented, hit.tied, midiScratch);   // MIDI mode: a note-on now; otherwise the hit waits for render
        }
        const float frac = (float) std::fmod(resolved.beat / RhythmSequencer::StepLengthBeats, 1.0);
        publishPlayState(cursor.lastStep(), cursor.lastAccentStep(), cursor.patternLength(), frac, fired);
    }

    // The modulation pass's inputs: the slot's mixer strip and the clock's step.
    if (services != nullptr)
    {
        services->transportPlaying.store(playing, std::memory_order_relaxed);
        block.strip = services->stripFor(slot);
    }
    block.lastEffectiveStep = clock.lastEffective();
}

inline void SlotRhythm::flushMidi(const mu_core::RenderContext& ctx, int n)
{
    // The note-off countdown runs over every call (stopped included); a note-on from the hit is already in the scratch.
    midi.processBlock(midiScratch, n);
    for (const auto meta : midiScratch)
        if (meta.numBytes >= 1)
            ctx.emitMidi(meta.samplePosition, meta.data[0], meta.numBytes > 1 ? meta.data[1] : (uint8_t) 0,
                         meta.numBytes > 2 ? meta.data[2] : (uint8_t) 0, meta.numBytes);
    midiScratch.clear();
}

inline void SlotRhythm::render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n)
{
    if (ctx.frameOffset == 0)
        startBlock(ctx);
    SampleRhythm::render(ctx, out, n);   // the voice retrigger, the modulation pass, then the voice
    flushMidi(ctx, n);
}

// The sample a node names, resolved against the Samples folder at the host's request.
inline RhythmPresetContext SlotRhythm::presetContext() const
{
    RhythmPresetContext ctx;
    ctx.sampleRate = preparedRate > 0.0 ? preparedRate : 44100.0;
    ctx.blockSize  = preparedBlock > 0 ? preparedBlock : 512;
    if (services != nullptr)
    {
        ctx.samplesDir   = services->getSamplesDir();
        ctx.onLoadError  = [s = services](const juce::String& message) { s->reportLoadError(message); };
    }
    return ctx;
}

inline juce::String SlotRhythm::sampleKey(const juce::ValueTree& node)
{
    return node.getProperty("sample").toString() + "|" + juce::String(node.getProperty("sampleData").toString().hashCode64());
}

inline void SlotRhythm::writeExtras(juce::ValueTree& node) const
{
    SampleRhythm::writeExtras(node);   // the modulators
    if (services == nullptr) return;
    node.setProperty("sample", rhythm_preset::toRelativeSamplePath(services->samples.path(slot), services->getSamplesDir()), nullptr);
}

inline void SlotRhythm::preloadStaged(const juce::ValueTree& stagedNode)
{
    if (services == nullptr) return;
    // The file work now, in a scratch engine: its buffer is moved into the voice at the boundary.
    VoiceEngine scratch;
    staged.key    = sampleKey(stagedNode);
    staged.path   = rhythm_preset::loadPresetSample(scratch, stagedNode, "sample", "sampleData", "sampleName",
                                                    presetContext(), true, {}, "embedded");
    staged.sample = scratch.takeSample();
    staged.valid  = true;
}

inline void SlotRhythm::applyExtras(const juce::ValueTree& node)
{
    extrasApplied = true;
    SampleRhythm::applyExtras(node);   // the modulators
    if (services == nullptr || voice == nullptr) return;

    // The sample: the one staged for this node, else read now (a stopped load, a state restore).
    std::optional<juce::String> path;
    if (staged.valid && staged.key == sampleKey(node))
    {
        path = staged.path;
        if (path.has_value())
        {
            if (staged.sample.isValid()) voice->installSample(std::move(staged.sample));
            else                         voice->clearSample();
        }
    }
    else
    {
        path = rhythm_preset::loadPresetSample(*voice, node, "sample", "sampleData", "sampleName", presetContext(), true, {}, "embedded");
    }
    staged = {};
    if (path.has_value())
        services->samples.setPath(slot, *path);

    // A preset that lands while the sequencer runs starts its pattern from step 0 on the boundary it waited for, as a
    // mu-Clid hot-swap commit does; stopped, the next start does that anyway.
    if (services->transportPlaying.load(std::memory_order_relaxed))
        cursor.requestSwapStart(*this, kRhythmStepCap, cursor.boundaryEffective(), clock.currentEpoch());
}

// ── The type ─────────────────────────────────────────────────────────────────

// A fresh Rhythm for `slot` with its voice engine, not yet bound or prepared. The engine and the insert's delay lines are
// allocated here, off the audio thread and outside any lock; the per-slot seed keeps layers from sharing a noise sequence.
inline std::unique_ptr<Layer> createRhythm(int slot, mu_core::LayerTypeId /*type*/)
{
    auto r = std::make_unique<SlotRhythm>();
    r->voice = std::make_unique<VoiceEngine>();
    r->voice->insertProc.setSlotIndex(slot);
    return r;
}

// Wires a Rhythm to its processor's services; the services of the Rhythm type are always a RhythmSlotServices.
inline void attachRhythmServices(Layer& layer, mu_core::LayerServices& services, int slot)
{
    static_cast<SlotRhythm&>(layer).attachServices(static_cast<RhythmSlotServices&>(services), slot);
    static_cast<RhythmSlotServices&>(services).samples.clearPath(slot);   // a new layer carries no sample until its node says so
}

// ── .muRhythm: mu-Clid's file shape <-> the composed node a slot applies ──
// mu-Clid keeps a rhythm's parameters as r0_ properties on the root; a slot host applies rows. These convert at the
// file boundary, so a file saved by mu-Clid loads into a slot and the reverse (the sample, modulators and parameters
// travel; the file's name, colour, category and embedded sample are not a slot's to take).

inline juce::ValueTree rhythmPresetFileToNode(const juce::ValueTree& file, const juce::String& name)
{
    // The same reader mu-Clid loads with (migrations included); no file work, nothing reported.
    const Rhythm r = rhythm_preset::readRhythm(file, "r0_", RhythmPresetContext {}, name);

    juce::ValueTree node(file.getType());
    for (int j = 0; j < kRhythmParamCount; ++j)
    {
        juce::ValueTree row(mu_pp::kRowTag);
        row.setProperty("id", kRhythmParamDefs[j].suffix, nullptr);
        row.setProperty("x", kRhythmParamDefs[j].push(r), nullptr);
        node.appendChild(row, nullptr);
    }
    node.appendChild(mu_pp::serialiseModulators(r), nullptr);
    node.setProperty("sample", file.getProperty("r0_sample"), nullptr);
    for (const char* embedded : { "sampleData", "sampleDataCodec", "sampleDataBits", "sampleName" })
        if (file.hasProperty(embedded))
            node.setProperty(embedded, file.getProperty(embedded), nullptr);
    return node;
}

inline juce::ValueTree rhythmPresetNodeToFile(const juce::ValueTree& node, const juce::String& name)
{
    juce::ValueTree file(node.getType());
    file.setProperty("presetName", name, nullptr);
    file.setProperty("presetCategory", juce::String(), nullptr);
    file.setProperty("presetDescription", juce::String(), nullptr);
    file.setProperty("presetEmbedSamples", 0, nullptr);
    file.setProperty("r0_name", name, nullptr);
    file.setProperty("r0_colour", 0, nullptr);

    // A sample decoded from an embedded preset lives in a temp folder that does not survive the session: not a file's to name.
    juce::String sample = node.getProperty("sample").toString();
    if (rhythm_preset::isEmbeddedSampleTempPath(sample)) sample = {};
    file.setProperty("r0_sample", sample, nullptr);

    for (int i = 0; i < node.getNumChildren(); ++i)
    {
        const auto row = node.getChild(i);
        if (! row.hasType(mu_pp::kRowTag)) continue;
        if (const auto* def = findRhythmParamDef(row.getProperty("id").toString()))
            writeKindedProperty(file, "r0_" + juce::String(def->suffix), (float) (double) row.getProperty("x"), def->kind, def->algorithmNames);
    }
    if (const auto mods = node.getChildWithName("Modulators"); mods.isValid())
        file.appendChild(mods.createCopy(), nullptr);
    return file;
}

inline mu_core::LayerTypeInfo rhythmLayerTypeInfo() noexcept
{
    mu_core::LayerTypeInfo info;
    info.id = mu_core::LayerTypeId::Rhythm;
    info.name = "Rhythm";
    info.group = "mu-Clid";
    info.paramPrefixLetter = 'r';
    info.presetExtension = ".muRhythm";
    info.contentFolderName = "muClid";   // the layer presets and samples are mu-Clid's own, shared with it, not copied
    info.presetFolder = "Rhythms";
    info.presetTag = kRhythmPresetTag;
    info.descriptor = "Euclidean sample rhythm";
    info.discInitial = "E";   // Euclid: "R" is Rumble's in a host that offers both
    info.addLayerParams = &addLayerParams;
    info.create = &createRhythm;
    info.attachServices = &attachRhythmServices;
    info.presetFileToNode = &rhythmPresetFileToNode;
    info.presetNodeToFile = &rhythmPresetNodeToFile;
    return info;
}

} // namespace mu_clid
