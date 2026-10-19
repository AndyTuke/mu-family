#pragma once

#include "Plugin/ProcessorBase.h"            // mu-core base
#include "Plugin/MixerFxParams.h"            // mu-core: shared global-FX/mixer APVTS layout
#include "Sequencer/Pattern.h"           // mu-Tant layer type (derives mu-core Layer)
#include "Persistence/PatternParams.h"   // the Pattern type's parameter ids and rows
#include "UI/PatternHost.h"               // what the Pattern layer and its UI read from their host
#include "Sequencer/GatePattern.h"           // mu-tant: per-voice gate pattern
#include "Audio/SynthVoice.h"                // mu-tant voice
#include "Audio/Wavetable/WavetableBank.h"   // mu-core
#include "Audio/InsertProcessor.h"           // mu-core: shared per-voice insert FX

#include "Plugin/MidiClockSync.h"            // mu-core: shared MIDI-clock slave
#include "License/LicenseKey.h"              // product: mu-Tant id + filename + public key

#include <array>
#include <atomic>
#include <memory>

// mu-tant — wavetable drone synth.
//
// 1–8 free-running voices, each with two wavetable oscillators (cross-mod /
// FM / Sync), scale-quantised pitch, mu-core filter + drive + lo-cut, a
// per-voice drawable gater + filter envelope + pitch envelope, and a shared
// insert effect. Each voice is a Pattern layer that renders itself through the
// shared layer driver (renderAndMix) into the shared MixerEngine (FX sends +
// sidechain + returns + master); the Note-mode gate multiplies the final mix.
namespace mu_tant
{

class PluginProcessor : public ProcessorBase,
                        public PatternHost
{
public:

    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this
    // listener (mirrors mu-clid). Voice-engine params (v{N}_*) are read per-block
    // via cached pointers instead, so they're not listened to here.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { resetSwapPlayState(); releaseLayerRender(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tant")); }
    juce::String getProductSlug() const override { return "mu-tant"; }   // key in latest.json
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Internal transport (drives the gate engine + the gating timeline) ─────
    // mu-tant has no host-sync sequencer; the transport bar's play button starts
    // / stops an internal free-running clock. While stopped the beat freezes at 0 and
    // the gater is CLOSED — so the voice is silent on load (gateModeFor: !playing →
    // Silence). To audition the raw oscillator drone, bypass the gater (gate_bypass →
    // Pass). While playing the beat advances and the gate engine chops per the pattern.

    // Master loop — length from the mstrLoop param (0 = free; 1..16 → 16..256 steps),
    // live step derived from the free-running beat (4 steps/beat = 16 steps/bar, same
    // step grid as mu-clid). UI-thread reads only (the shared MasterLoopSection timer).
    int getMasterLoopSteps() const override
    {
        const auto* p = mstrLoopPtr ? mstrLoopPtr : apvts.getRawParameterValue("mstrLoop");
        return p ? (int) p->load() * 16 : 0;
    }
    int getMasterLoopCurrentStep() const override
    {
        const int steps = getMasterLoopSteps();
        if (steps <= 0) return 0;
        const double loopBeats = steps / 4.0;
        const double pos = std::fmod(getInternalBeatPos(), loopBeats);
        return juce::jlimit(0, steps - 1, (int) (pos * 4.0));
    }

    // MIDI Note mode (plugin DAW notes + standalone keyboard). Free = the drone runs
    // continuously (default). Note = gate + pitch-track — the drone sounds only while
    // a MIDI note is held, and the held note (last-note priority) sets the tonal centre.
    // 0 = Free, 1 = Note. Setter persists to appSettings.
    void setMidiNoteMode(int mode);
    int  getMidiNoteMode() const { return midiNoteMode.load(std::memory_order_relaxed); }

    // ── ProcessorBase channel metadata ───────────────────────────────────────
    // mu-tant manages a dynamic set of voices ("layers") exactly like mu-clid's
    // rhythms — there are no inactive voices, only the ones that exist. The
    // count is `numLayers` (1..mu_limits::kMaxLayers, owned by ProcessorCore, which also supplies
    // getNumLayers / getLayer over its layer storage); add/delete adjust it.
    juce::String getLayerName(int idx)       const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? juce::String("Voice ") + juce::String(idx + 1)
                                              : juce::String();
    }
    // Each voice carries an allocated palette-colour index (mu-clid's rule:
    // assigned first-unused on add, follows the voice through delete/reorder),
    // so layers are distinctly + stably coloured. It lives on the Layer, so it moves with the voice.
    int          getLayerColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < mu_limits::kMaxLayers) ? layers[(size_t) idx]->colourIndex : 0;
    }

    // ── Dynamic voice management (message-thread) ──
    // Adding, deleting, reordering and resetting a voice are ProcessorBase::addLayer / removeLayer /
    // swapLayers / resetLayer; this product supplies createLayer and cancelPendingSwap. The edits
    // move the Pattern pointers under `layersLock`, which processBlock tryLocks.
    bool canEditLayers() const override { return true; }
    // The engine state of a slot stays put while its Pattern moves, so the slot is cleared for the
    // new tenant (called under layersLock; the render only runs under it).
    void layersMoved(int firstIndex, int lastIndex) override;
    // Recompute the cached stepped-pitch flags for one voice / all voices (Pattern::refreshPitchQuantFlags).
    // Call on the message thread whenever a voice's modulators change (editor edit, preset load,
    // voice add/remove/swap/reset); the audio thread reads the flags lock-free.
    void refreshPitchQuantFlags(int v) { if (v >= 0 && v < mu_limits::kMaxLayers) getPattern(v).refreshPitchQuantFlags(); }
    void refreshAllPitchQuantFlags()   { for (int v = 0; v < mu_limits::kMaxLayers; ++v) refreshPitchQuantFlags(v); }

    // Per-voice ("layer") presets — the voice's `v{N}_*` subtree saved/loaded as
    // a `.muPattern` file (voice-agnostic base IDs, so a preset loads into any
    // slot). Mirrors mu-clid's per-rhythm preset I/O.
    void saveLayerPreset(int voice, const juce::String& name) override;
    void loadLayerPreset(int voice, const juce::File& file) override;

    juce::ValueTree captureLayerPresetTree(int voice) override;
protected:
    void stageLayerPresetTree(int voice, juce::ValueTree tree, bool keepIdentity) override;
public:

    // ── User wavetable import (per oscillator) ───────────────────────────────
    // Load a Serum/Vital .wav into the shared bank (dedup by path) and point the
    // given voice's oscillator at it; clear reverts to the factory selection.
    void         loadUserWavetable(int voice, int oscIndex, const juce::File& file);
    void         clearUserWavetable(int voice, int oscIndex);
    juce::String userWavetablePath(int voice, int oscIndex) const;    // "" = factory selection
    bool         userWavetableMissing(int voice, int oscIndex) const; // path set but file gone

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getLayerPresetDir()       const override;   // voice presets live here
    juce::String getLayerPresetExtension() const override { return "muPattern"; }
    juce::String getFullPresetExtension()    const override { return "muTant"; }
    juce::File   getWavetablesDir()          const;            // user/factory .wav wavetables live here

    // Full-preset save/load — the editor shell drives the UI (TransportBar
    // dropdown + Save dialog + Preset browser) and calls these. A preset is the
    // whole APVTS state wrapped with name/description/category metadata.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuTantPreset>.
    const char*     getFullPresetTag() const override { return "MuTantPreset"; }
    juce::ValueTree captureFullPreset() override;
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // A per-voice hot-swap commit fires ProcessorBase::onLayerPresetCommitted (the editor refreshes
    // that voice's panel, sidebar and wavetable dropdowns); full presets fire onPresetSwapCommitted.

    // The "SWP" badges and the cancel read the shared ProcessorBase::layerSwapper.

    // Per-voice param IDs are the Pattern type's: layerParamId(voice, base) in Persistence/PatternParams.h.

    // The APVTS layout factory (defined in PluginProcessor_APVTS.cpp). Public so
    // the layout test can exercise the real param set without constructing the
    // processor — pure static factory, no state.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Modulation snapshot accessor for PatternContent knob live-arc indicators.
    float getTantSnap(int voice, int snapIndex) const noexcept
    {
        if (voice < 0 || voice >= mu_limits::kMaxLayers) return 0.0f;
        return getPattern(voice).getModLive(snapIndex);
    }

protected:
    // MIDI program-change apply hooks (drained on the message thread). Ch 1-8 →
    // per-voice preset into the matching slot; Ch 9 → full preset. Both entry
    // points hot-swap (stage at the loop boundary when playing, apply immediately
    // when stopped).
    void applyFullMidiPreset(const juce::File& f)           override { loadPreset(f); }

    // A default voice for slot `slot`, built before a structural edit takes the lock.
    std::unique_ptr<Layer> createLayer(int slot) override;

private:
    // The boundary rule's parameter atomics (master loop, swap timing), resolved once in the constructor.
    void cacheParamPointers();

    WavetableBank                                          bank;
    // Per-instance noise seed salt, taken before the constructor body builds the voices; 0 when MU_DETERMINISTIC_NOISE is set (golden renders).
    const juce::int64 noiseSeedSalt = mu_core::makeNoiseSalt();
    // The slot engines, one per slot (not per voice): the Pattern in a slot renders through that slot's engine,
    // bound in Pattern::bindParams, so a voice that moves takes up the slot's oscillator phase (VoiceEngine::reset
    // keeps it), as it always did. The per-voice insert (shared mu-core InsertProcessor) is Pattern::insert — it
    // runs after the gate, before the pan/sum into the mixer (engine → insert → mixer, the family signal flow).
    // A count-reducing preset's retire fade is the Pattern's own, in the shared retire bin (applyFullPresetTree).
    std::array<std::unique_ptr<VoiceEngine>, mu_limits::kMaxLayers>   voices;

public:
    // ── PatternHost: what the Pattern layer and PatternContent read (message thread) ──────────
    juce::AudioProcessorValueTreeState& getPatternParameters() noexcept override { return apvts; }
    Pattern&     getPatternLayer(int layer) noexcept override                     { return getPattern(layer); }
    float        getPatternModLive(int layer, int snap) const noexcept override    { return getTantSnap(layer, snap); }
    const std::atomic<float>* getPatternInsertGainReduction(int layer) const override { return getInsertGR(layer); }
    bool         isPatternTransportPlaying() const noexcept override              { return isInternalPlaying(); }
    double       getPatternTransportBeat() const noexcept override                { return getInternalBeatPos(); }
    juce::File   getPatternWavetablesDir() const override                         { return getWavetablesDir(); }
    juce::String getPatternWavetablePath(int layer, int oscIndex) const override  { return userWavetablePath(layer, oscIndex); }
    bool         isPatternWavetableMissing(int layer, int oscIndex) const override { return userWavetableMissing(layer, oscIndex); }
    void         loadPatternWavetable(int layer, int oscIndex, const juce::File& file) override { loadUserWavetable(layer, oscIndex, file); }
    void         clearPatternWavetable(int layer, int oscIndex) override          { clearUserWavetable(layer, oscIndex); }
    int          resolvePatternWavetable(const juce::String& path) override;
    VoiceEngine* getPatternEngine(int slot) noexcept override
    {
        return slot >= 0 && slot < mu_limits::kMaxLayers ? voices[(size_t) slot].get() : nullptr;
    }
    const juce::CriticalSection* getPatternBankLock() const noexcept override { return &bankLock; }

    // Voice `i`'s Pattern (modulators, gate / filter / pitch envelopes, user wavetables, level tap),
    // valid for every slot 0..kMaxLayers-1. Public so the UI (ModulatorPanel, PatternContent) can point
    // at the currently-edited voice.
    Pattern&       getPattern(int i) noexcept       { return layerAs<Pattern>(i); }
    const Pattern& getPattern(int i) const noexcept { return layerAs<Pattern>(i); }

private:
    // Master-loop length param pointer, cached for RT-safe reads (no per-block
    // string lookup) in processBlock. Set in cacheParamPointers().
    const std::atomic<float>* mstrLoopPtr { nullptr };
    // Hot-swap timing: where a staged preset / program-change swap commits (the shared `mstr_swap`
    // parameter; mu_hotswap::SwapTiming). A full preset always uses the master loop when one is
    // defined, else voice 0's gate boundary. Resolved once in cacheParamPointers().
    const std::atomic<float>* swapTimingPtr { nullptr };

    // ── MIDI Note mode: the final-mix gate (decision A3; each Pattern pitch-tracks its own held keys) ──
    // 0 = Free, 1 = Note. Set from the Settings overlay (message thread, which also tells every Pattern), read
    // on the audio thread. The rest of the state below is touched ONLY on the audio thread (scanned + applied
    // within processBlock on one thread) so it needs no synchronisation.
    std::atomic<int> midiNoteMode { 0 };
    HeldNoteStack    gateNotes;              // the keys held, every host block (last-note priority)
    float            noteGateGain   = 1.0f;  // ramped amplitude gate (anti-click)
    int              lastNoteMode   = 0;     // detects a Free↔Note switch → reset state
    // Scans note on/off of the whole host block into the gate's held-note stack. Run before the render (also
    // for a block the driver does not render). No-op in Free mode.
    void scanNoteMode(const juce::MidiBuffer& midi);
    // Advances the anti-click amplitude ramp toward the held/silent target and applies
    // it to the final mix. Run after the render. No-op in Free mode (gate stays open).
    void applyNoteModeGate(juce::AudioBuffer<float>& buffer, int numSamples);

    // The hot-swap boundary flagging of one block (audio thread): mu-Tant's rule (swapLoopBeats) over each
    // slot's gate-pattern length in beats, the swap timing and the master loop.
    void flagPatternSwapBoundaries(bool playing, double startBeat, double blockBeats, const double* patternBeats) noexcept;
    // Under layersLock: a layer built after the constructor takes the held keys every other layer has heard.
    void seedInputState(int slot) noexcept;

    // The prepared rate and block size: written by prepareToPlay (a host thread), read by createLayer (message
    // thread) and the final-mix gate (audio thread), so atomic.
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int>    currentBlockSize  { 512 };    // with the rate, how a new Pattern is prepared

    // The voice count (ProcessorCore::numLayers) and the per-voice data shift during add/remove are guarded
    // against the audio thread by ProcessorCore::layersLock, which the shared driver try-locks once per block
    // (a miss silences the block: a sub-millisecond gap while a voice is added or removed).
    // Guards the wavetable bank append against the audio render (which reads the bank), kept apart from
    // layersLock so an import never blocks a layer edit and the reverse. Each Pattern try-locks it per call
    // (after the driver's layersLock: the old acquire order); a miss silences that voice for the call.
    juce::CriticalSection bankLock;

    // Composed state (mu-core LayerState): the voice layout every save / load uses (a voice's
    // non-parameter data is the Pattern's own writeExtras / applyExtras), and any saved state rebuilt
    // in the composed shape with old X-Mod data migrated.
    void            initVoiceState();
    juce::ValueTree toVoiceState(const juce::ValueTree& tree) const;

    // ── Preset hot-swap (full / per-voice) ─────────────────────────
    // loadPreset / loadLayerPreset stage the parsed tree when the transport is
    // playing (commit at the loop boundary) and apply immediately when stopped.
    // The apply bodies are factored out so the boundary commit (runDeferredWork)
    // and the immediate path share one code path.
    void applyFullPresetTree (const juce::ValueTree& state);          // composed state + voice count / colours + FX
    void applyVoicePresetTree(int voice, const juce::ValueTree& tree); // one voice node (.muPattern body)
    // Warm the wavetable bank (dedup-by-path) for every user wavetable referenced
    // by a staged tree, so the boundary commit does no disk I/O. Lock-safe vs the
    // audio thread (bank append under bankLock).
    void preloadWavetablesFromState(const juce::ValueTree& state);     // full: walk the voice <Slot>s
    void preloadWavetablesFromVoiceTree(const juce::ValueTree& voiceTree); // per-voice: o1/o2WtPath
    // The bank index of a wavetable file: found by path, else decoded OFF bankLock and appended under
    // it (microseconds), so an import never silences the render for the decode. -1 when unreadable.
    int  bankIndexFor(const juce::File& file);

    // Register mixer/FX param listeners + run an initial engine sync (JUCE
    // doesn't fire parameterChanged on construction or for unchanged values, so
    // we seed mixerEngine/fxChain explicitly here + after a preset load).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
