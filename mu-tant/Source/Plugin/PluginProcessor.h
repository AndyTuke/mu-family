#pragma once

#include "Plugin/ProcessorBase.h"            // mu-core base
#include "Plugin/MixerFxParams.h"            // mu-core: shared global-FX/mixer APVTS layout
#include "Sequencer/Layer.h"             // mu-core: per-voice modulator data container
#include "Modulation/LaneModulation.h"       // mu-core: shared range-based per-lane resolve
#include "Sequencer/GatePattern.h"           // mu-tant: per-voice gate pattern
#include "Audio/SynthVoice.h"                // mu-tant voice
#include "Audio/Wavetable/WavetableBank.h"   // mu-core
#include "Audio/InsertProcessor.h"           // mu-core: shared per-voice insert FX

#include "Modulation/MuTantModSnap.h"
#include "Plugin/VoiceHotSwapStager.h"       // mu-tant: preset hot-swap staging
#include "Plugin/MidiClockSync.h"            // mu-core: shared MIDI-clock slave
#include "License/LicenseKey.h"              // product: mu-Tant id + filename + public key

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

// mu-tant — wavetable drone synth.
//
// 1–8 free-running voices, each with two wavetable oscillators (cross-mod /
// FM / Sync), scale-quantised pitch, mu-core filter + drive + lo-cut, a
// per-voice drawable gater + filter envelope + pitch envelope, and a shared
// insert effect. processBlock routes through the shared MixerEngine
// (FX sends + sidechain + returns + master) via the renderVoiceCb hook.
namespace mu_tant
{

// Lock-free audio-to-UI ring buffer — written by the audio thread in renderVoice()
// after the insert, read by VoiceSpectrumGlyph at 30 Hz to drive the sidebar animation.
// kSize must be a power of 2 (enables fast bit-mask indexing).
struct VoiceRingBuffer
{
    static constexpr int kSize = 1024;   // ~21 ms at 48 kHz

    // Audio thread: mono-mix buf and append n frames.
    void write(const juce::AudioBuffer<float>& buf, int n) noexcept
    {
        const int nCh  = buf.getNumChannels();
        const float sc = nCh > 0 ? 1.0f / (float) nCh : 0.0f;
        int head = writeHead.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < nCh; ++c)
                s += buf.getSample(c, i);
            data[(size_t)(head & (kSize - 1))] = s * sc;
            ++head;
        }
        writeHead.store(head, std::memory_order_release);
    }

    // UI thread: copy the most-recent n samples into out[].
    void read(float* out, int n) const noexcept
    {
        const int head  = writeHead.load(std::memory_order_acquire);
        const int start = head - n;
        for (int i = 0; i < n; ++i)
            out[i] = data[(size_t)((start + i) & (kSize - 1))];
    }

    std::array<float, kSize> data {};
    std::atomic<int>         writeHead { 0 };
};

class PluginProcessor : public ProcessorBase
{
public:
    // Family parity with mu-clid (max 8 rhythms / 8 voices / 8 channels).
    static constexpr int kMaxVoices = mu_limits::kMaxLayers;   // the family layer cap

    PluginProcessor();

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this
    // listener (mirrors mu-clid). Voice-engine params (v{N}_*) are read per-block
    // via cached pointers instead, so they're not listened to here.

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return juce::String (juce::CharPointer_UTF8 ("\xce\xbc-Tant")); }
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

    // Message-thread: commit any hot-swap that reached its loop boundary (ProcessorBase
    // then drains the MIDI program-change queue). Triggered from processBlock via
    // triggerAsyncUpdate() when the audio thread flags a boundary.
    void commitDeferredWork() override;

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

    // Hot-swap timing (mirrors mu-clid). OnVoiceLoop = the per-voice gate boundary.
    enum class SwapMode { OnMasterLoop = 0, OnVoiceLoop = 1 };
    SwapMode getSwapMode() const { return (SwapMode) swapModeAtomic.load(std::memory_order_relaxed); }
    void     setSwapMode(SwapMode m) { swapModeAtomic.store((int) m, std::memory_order_relaxed); }

    // MIDI Note mode (plugin DAW notes + standalone keyboard). Free = the drone runs
    // continuously (default). Note = gate + pitch-track — the drone sounds only while
    // a MIDI note is held, and the held note (last-note priority) sets the tonal centre.
    // 0 = Free, 1 = Note. Setter persists to appSettings.
    void setMidiNoteMode(int mode);
    int  getMidiNoteMode() const { return midiNoteMode.load(std::memory_order_relaxed); }

    // ── ProcessorBase channel metadata ───────────────────────────────────────
    // mu-tant manages a dynamic set of voices ("layers") exactly like mu-clid's
    // rhythms — there are no inactive voices, only the ones that exist. The
    // count is `numVoices` (1..kMaxVoices); add/delete adjust it.
    int          getNumChannels()              const override { return numVoices.load(std::memory_order_relaxed); }
    juce::String getChannelName(int idx)       const override
    {
        return (idx >= 0 && idx < kMaxVoices) ? juce::String("Voice ") + juce::String(idx + 1)
                                              : juce::String();
    }
    // Each voice carries an allocated palette-colour index (mu-clid's rule:
    // assigned first-unused on add, follows the voice through delete/reorder),
    // so layers are distinctly + stably coloured.
    int          getChannelColourIndex(int idx) const override
    {
        return (idx >= 0 && idx < kMaxVoices) ? voiceColourIndex[(size_t) idx] : 0;
    }

    // ── Dynamic voice management (message-thread; mirrors mu-clid add/delete) ──
    // addVoice appends a fresh default voice (returns its index, or -1 if full).
    // removeVoice deletes a voice, shifting every higher voice's APVTS values +
    // gate/modulator data down so the set stays contiguous. The last voice can't
    // be removed. Both hold `voicesLock`; processBlock tryLocks it.
    int  getNumVoices() const noexcept { return numVoices.load(std::memory_order_relaxed); }
    int  addVoice();
    void removeVoice(int idx);
    void swapVoices(int a, int b);   // reorder (drag in the sidebar)
    void remapSidechainSources(const std::function<int(int)>& remap);   // re-point scSrc after a renumbering
    void resetSlot(int idx) override;   // reset a voice to defaults (keeps its colour)
    // Recompute the cached stepped-pitch flags for one voice / all voices. Call on the
    // message thread whenever a voice's modulators change (editor edit, preset load,
    // voice add/remove/swap/reset); the audio thread reads the flags lock-free.
    void refreshPitchQuantFlags(int v);
    void refreshAllPitchQuantFlags();

    // Per-voice ("layer") presets — the voice's `v{N}_*` subtree saved/loaded as
    // a `.muPattern` file (voice-agnostic base IDs, so a preset loads into any
    // slot). Mirrors mu-clid's per-rhythm preset I/O.
    void saveSlotPreset(int voice, const juce::String& name) override;
    void loadSlotPreset(int voice, const juce::File& file) override;

    // ── User wavetable import (per oscillator) ───────────────────────────────
    // Load a Serum/Vital .wav into the shared bank (dedup by path) and point the
    // given voice's oscillator at it; clear reverts to the factory selection.
    void         loadUserWavetable(int voice, int oscIndex, const juce::File& file);
    void         clearUserWavetable(int voice, int oscIndex);
    juce::String userWavetablePath(int voice, int oscIndex) const;    // "" = factory selection
    bool         userWavetableMissing(int voice, int oscIndex) const; // path set but file gone

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getPerSlotPresetDir()       const override;   // voice presets live here
    juce::String getPerSlotPresetExtension() const override { return "muPattern"; }
    juce::String getFullPresetExtension()    const override { return "muTant"; }
    juce::File   getWavetablesDir()          const;            // user/factory .wav wavetables live here

    // Full-preset save/load — the editor shell drives the UI (TransportBar
    // dropdown + Save dialog + Preset browser) and calls these. A preset is the
    // whole APVTS state wrapped with name/description/category metadata.
    // Full presets: the standard ProcessorBase save / load / categories, under <MuTantPreset>.
    const char*     getFullPresetTag() const override { return "MuTantPreset"; }
    juce::ValueTree captureFullPreset() override;
    void            useLoadedFullPreset(juce::ValueTree state) override;

    // A per-voice hot-swap commit fires ProcessorBase::onSlotPresetCommitted (the editor refreshes
    // that voice's panel, sidebar and wavetable dropdowns); full presets fire onPresetSwapCommitted.

    // Hot-swap staging queries — drive the shared "SWP" badges (TransportBar for a
    // pending full preset, ChannelSidebar for a pending per-voice swap) + cancel.
    // Mirrors mu-clid. Polled by the UI timers.
    bool hasPendingFullPreset() const override { return hotSwapStager.hasFullPending(); }
    bool hasPendingSwap(int voice) const override { return hotSwapStager.hasVoicePending(voice); }
    void cancelStagedSwap(int voice)           { hotSwapStager.cancelVoice(voice); }

    // Demo limits the unlicensed editor to a single voice whose patterns have at most 16 steps.
    int demoMaxChannels() const override { return 1; }
    int demoMaxSteps()    const override { return 16; }

    // ── Per-voice param IDs (used by VoicePanel for SliderAttachment binding) ──
    // Family rule: per-voice params are subtree-scoped via `v{N}_` prefix so a
    // ValueTree restore round-trips cleanly + presets can target a specific
    // voice without name collisions across the 8 slots.
    static juce::String voiceParamId(int voice, const juce::String& base)
    {
        return juce::String("v") + juce::String(voice) + "_" + base;
    }

    // The APVTS layout factory (defined in PluginProcessor_APVTS.cpp). Public so
    // the layout test can exercise the real param set without constructing the
    // processor — pure static factory, no state.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // GR-meter source for the shared InsertSubsection (Compressor / Limiter P2).
    // Points at the per-voice insert's atomic reduction value; null when oob.
    const std::atomic<float>* getInsertGRPtr(int voice) const noexcept
    {
        if (voice < 0 || voice >= kMaxVoices) return nullptr;
        return &inserts[(size_t) voice].grReduction;
    }

    // Modulation snapshot accessor for VoicePanel knob live-arc indicators.
    float getTantSnap(int voice, int snapIndex) const noexcept
    {
        if (voice < 0 || voice >= kMaxVoices) return 0.0f;
        return voiceSnap[(size_t) voice][(size_t) snapIndex].load();
    }

protected:
    // MIDI program-change apply hooks (drained on the message thread). Ch 1-8 →
    // per-voice preset into the matching slot; Ch 9 → full preset. Both entry
    // points hot-swap (stage at the loop boundary when playing, apply immediately
    // when stopped).
    void applyFullMidiPreset(const juce::File& f)           override { loadPreset(f); }

private:
    VoiceConfig readConfig(int voiceIndex) const;

    // Cached APVTS atomic pointers. The pointers returned by getRawParameterValue
    // are stable for the processor's lifetime (replaceState changes values, not
    // the parameter objects), so we resolve them once in the constructor and read
    // them in processBlock — never rebuilding juce::String IDs on the audio thread.
    struct VoicePtrs
    {
        std::atomic<float> *o1Oct, *o1Semi, *o1Fine, *o1Pos, *o1Wt;
        std::atomic<float> *o2Oct, *o2Semi, *o2Fine, *o2Pos, *o2Wt;
        std::atomic<float> *xmodPhaseMode, *xmodIndex, *sync, *xmodFdbk, *xmodAmpMode, *xmodDepth, *xmodSsb;
        std::atomic<float> *o1Lvl, *o2Lvl, *noiseLvl, *noiseType;
        std::atomic<float> *fltType,  *fltCut,  *fltRes,  *fltEnvDepth,  *fltDrv,  *fltLoCut;
        std::atomic<float> *flt2Type, *flt2Cut, *flt2Res, *flt2EnvDepth, *flt2Drv, *flt2LoCut;
        std::atomic<float> *fltSeries;
        std::atomic<float> *o1PenvDepth, *o2PenvDepth;
        std::atomic<float> *level, *gateGap, *gateBypass;
        std::atomic<float> *drvChar, *insP1, *insP2, *insP3, *insP4;
    };
    std::array<VoicePtrs, kMaxVoices> voicePtrs {};
    struct GlobalPtrs { std::atomic<float> *root, *scale; } globalPtrs {};
    void cacheParamPointers();

    // Per-channel render hook handed to the shared MixerEngine: runs one voice's
    // modulation → engine → gate → insert into the channel buffer (engine→insert→
    // mixer). Captures only `this`; the per-block transport snapshot it reads lives
    // in the blk* members below, set at the top of processBlock (same thread).
    MixerEngine::RenderChannelFn renderVoiceCb;
    void   renderVoice(int voiceIndex, juce::AudioBuffer<float>& buf, int numSamples);
    // Render a voice being retired by a count-reducing full-preset swap: its OLD
    // (pre-swap) config through its still-intact engine+insert, under a falling gain.
    void   renderRetiringVoice(int v, juce::AudioBuffer<float>& buf, int numSamples);
    // renderVoice phases — called in order; each handles one concern.
    void   applyModulation    (int v, VoiceConfig& cfg);
    void   applyFilterEnvelope(int v, VoiceConfig& cfg, int numSamples);
    void   applyPitchEnvelope (int v, VoiceConfig& cfg);
    // True if a Stepped ControlSequence is assigned to `destId` — those snap pitch to
    // semitones (melodies); smooth sources + the envelope glide.
    bool   pitchDestHasSteppedSource(const Layer& slot, const char* destId) const;
    bool   blkPlaying        = false;
    double blkBeatStart      = 0.0;
    double blkBeatsPerSample = 0.0;

    WavetableBank                                          bank;
    std::array<std::unique_ptr<VoiceEngine>, kMaxVoices>   voices;
    // Per-voice insert effect (shared mu-core InsertProcessor) — runs after the
    // gate, before the pan/sum into the mixer (engine → insert → mixer, the
    // family-wide signal flow). Mirrors mu-clid's per-rhythm insert.
    std::array<InsertProcessor,              kMaxVoices>   inserts;

    // ── Retire-tail for count-reducing full-preset swaps ──────────────────────
    // A committed full preset with fewer voices would hard-cut the dropped voices
    // (the render loop just sums fewer channels), snapping their comb-filter / insert
    // tail. Instead we snapshot each dropped voice's OLD config before replaceState
    // and keep rendering it through its (intact) engine+insert under a falling gain
    // for ~25 ms, then stop — so the tail fades. `samplesLeft` is the audio-thread
    // countdown; `config` is written (message thread) before `samplesLeft` is armed
    // (release) and read only when samplesLeft>0 (acquire), so no lock is needed.
    struct RetiringVoice
    {
        std::atomic<int>    samplesLeft { 0 };
        float               gainStep = 0.0f;   // per-sample gain decrement (= 1/rampLen)
        VoiceConfig         config;            // OLD engine config (osc + filter)
        int                 insAlgo = 0;       // OLD insert algo + params, so the insert
        std::array<float,4> insP {};           // tail (e.g. a reverb the new preset drops) fades too
    };
    std::array<RetiringVoice, kMaxVoices> retiring {};
    int retireRampSamples() const { return juce::jmax(1, (int) (currentSampleRate * 0.025)); }

public:
    // Per-voice modulator data — 8 ControlSequences + ModulationMatrix + modLock
    // per voice. Public so the UI (ModulatorPanel) can pass a pointer to the
    // currently-edited voice's slot.
    std::array<Layer, kMaxVoices> voiceSlots;

    // Per-voice drawable gate pattern. Public so GatingDesigner can mutate it.
    std::array<GatePattern, kMaxVoices> gatePatterns;

    // Per-voice filter envelope pattern. Same drawable model as gatePatterns but
    // modulates filter cutoff (0=20 Hz, 1=base cutoff) instead of amplitude.
    std::array<GatePattern, kMaxVoices> filterPatterns;

    // Per-voice pitch envelope pattern. Envelope value (0..1) × depth (±24 st)
    // adds semitones to osc1/osc2 pitch on each block.
    std::array<GatePattern, kMaxVoices> pitchPatterns;

    // Per-voice post-insert audio ring buffers — written by the audio thread in
    // renderVoice() after the insert; read by VoiceSpectrumGlyph at 30 Hz for
    // the sidebar spectrum animation.
    std::array<VoiceRingBuffer, kMaxVoices> voiceRingBuffers;

private:
    // Per-voice modulated-value snapshots — written by the audio thread in
    // renderVoice() after the matrix runs; read by VoicePanel at ~30 Hz via
    // getTantSnap() to drive live-arc indicators on bound knobs.
    std::array<std::atomic<float>, mu_tant::kTantSnapCount> voiceSnap[kMaxVoices];

    // Pre-allocated modulation paramValues map — reused every block to avoid
    // audio-thread allocation. Keys match the strings in MuTantModDest::kModDestTable.
    // Values are seeded each block from the current VoiceConfig and read back
    // after the matrix runs.
    // THREADING: MixerEngine calls renderVoice sequentially (one voice at a time)
    // so this map is never accessed concurrently — safe to share across voices.
    // If MixerEngine ever renders channels in parallel, move this into a per-voice
    // structure to avoid a data race.
    std::unordered_map<std::string_view, float> modParamValues;

    // Cached modulation-destination routing for mu_mod::resolveLane (built once in
    // cacheParamPointers). `modDestIds` are the kModDestTable ids (proportion-space keys);
    // `modDestRanges` each param's NormalisableRange (voice-independent); `modDestAtoms` the
    // per-voice backing APVTS atomics. resolveLane seeds proportions from the atoms, runs the
    // voice's matrix under a try-lock, and writes the modulated values back in param units.
    static constexpr int kNumModDests = 31;   // == mu_tant::kModDestCount (asserted in the .cpp)
    std::array<const char*, kNumModDests>                    modDestIds   {};
    std::array<juce::NormalisableRange<float>, kNumModDests> modDestRanges{};
    std::array<std::array<const std::atomic<float>*, kNumModDests>, kMaxVoices> modDestAtoms{};

    // Per voice: does a Stepped CS drive osc{1,2}.semi? Computed on the message thread when
    // modulators change (refreshPitchQuantFlags), read lock-free by the audio thread —
    // stepped pitch snaps to semitones, smooth glides.
    std::array<std::atomic<bool>, kMaxVoices> osc1SemiStepped {};
    std::array<std::atomic<bool>, kMaxVoices> osc2SemiStepped {};

    // Master-loop length param pointer, cached for RT-safe reads (no per-block
    // string lookup) in processBlock. Set in cacheParamPointers().
    const std::atomic<float>* mstrLoopPtr { nullptr };
    // Hot-swap timing: which loop a staged preset / program-change swap commits on.
    // 0 = OnMasterLoop (default) — commit at the master-loop wrap when a loop is set;
    // 1 = OnVoiceLoop — commit at the per-voice gate-pattern boundary. A full preset
    // always uses the master loop when one is defined, else voice 0's gate boundary.
    std::atomic<int> swapModeAtomic { 0 };

    // ── MIDI Note mode (gate + pitch-track) ──────────────────────────────────
    // 0 = Free, 1 = Note. Set from the Settings overlay (message thread), read on
    // the audio thread. The rest of the note-mode state below is touched ONLY on
    // the audio thread (scanned + applied within processBlock/renderVoice on one
    // thread) so it needs no synchronisation.
    std::atomic<int>    midiNoteMode { 0 };
    std::array<int, 16> heldNotes {};         // held-note stack (last-note priority)
    int                 numHeldNotes   = 0;
    int                 noteCurrentMidi = -1; // top of stack (-1 = none) → pitch-track
    float               noteGateGain   = 1.0f;// ramped amplitude gate (anti-click)
    int                 lastNoteMode   = 0;   // detects a Free↔Note switch → reset state
    // Scans note on/off into the held-note stack → updates noteCurrentMidi (drives
    // pitch-track in renderVoice). Run before the render. No-op in Free mode.
    void scanNoteMode(const juce::MidiBuffer& midi);
    // Advances the anti-click amplitude ramp toward the held/silent target and applies
    // it to the final mix. Run after the render. No-op in Free mode (gate stays open).
    void applyNoteModeGate(juce::AudioBuffer<float>& buffer, int numSamples);

    // Shared MIDI-clock slave (standalone). process() scans the MIDI buffer each block;
    // when enabled + playing, processBlock slaves the beat/tempo to it.
    // Written in prepareToPlay (host suspends the audio thread first) — no atomic needed.
    double currentSampleRate = 44100.0;

    // Number of existing voices (layers), 1..kMaxVoices. Audio thread reads it
    // atomically; add/removeVoice mutate it on the message thread under voicesLock.
    std::atomic<int> numVoices { 1 };
    // Per-voice palette-colour index (0..7). Default identity; addVoice assigns
    // the first-unused colour, remove/swap shift it so colour follows the voice.
    std::array<int, kMaxVoices> voiceColourIndex { { 0, 1, 2, 3, 4, 5, 6, 7 } };
    // Per-voice user-imported wavetable path (one per oscillator) — the source of
    // truth, following the voice through swap/save like voiceColourIndex. The
    // atomic resolved bank index is what the audio thread reads each block;
    // -1 = no user table → fall back to the factory o{1,2}_wt selection.
    std::array<juce::String, kMaxVoices>     osc1UserPath, osc2UserPath;
    std::array<std::atomic<int>, kMaxVoices> osc1UserIndex, osc2UserIndex;
    int firstUnusedColourIndex() const;   // lowest palette index not used by an active voice
    // Guards the voice count + the per-voice data shift during add/remove against
    // the audio thread. processBlock takes a ScopedTryLock and silences the block
    // on contention (a sub-millisecond gap while a voice is added/removed).
    juce::CriticalSection voicesLock;

    // Copy every `v{src}_*` and `ch{src}_*` APVTS parameter value to the matching
    // `v{dst}_*` / `ch{dst}_*` parameter (used by removeVoice's down-shift).
    void copyVoiceParams(int src, int dst);
    // Reset one voice slot to defaults — APVTS params + gate pattern + modulators.
    void resetVoiceSlot(int idx);

    // Per-voice colour allocation persistence (stored on the APVTS state tree
    // alongside numVoices, so colours round-trip with full + per-... presets).
    juce::String serialiseVoiceColours() const;
    void         restoreVoiceColours(const juce::String& csv);

    // Composed state (mu-core LayerState): the voice layout every save / load uses, a voice's
    // non-parameter data (modulators, gates, user wavetables — they live outside APVTS) from its
    // node, and any saved state rebuilt in the composed shape with old X-Mod data migrated.
    void            initVoiceState();
    void            applyVoiceExtras(int v, const juce::ValueTree& node);
    void            resolveUserWavetable(const juce::String& path, juce::String& storedPath, std::atomic<int>& index);
    juce::ValueTree toVoiceState(const juce::ValueTree& tree) const;

    // ── Preset hot-swap (full / per-voice) ─────────────────────────
    // loadPreset / loadSlotPreset stage the parsed tree when the transport is
    // playing (commit at the loop boundary) and apply immediately when stopped.
    // The apply bodies are factored out so the boundary commit (handleAsyncUpdate)
    // and the immediate path share one code path.
    VoiceHotSwapStager hotSwapStager;
    // Previous block's play state (audio-thread only) — drives the playing→stopped
    // edge that commits a staged swap on stop.
    bool wasPlaying = false;
    void applyFullPresetTree (const juce::ValueTree& state);          // composed state + voice count / colours + FX
    void applyVoicePresetTree(int voice, const juce::ValueTree& tree); // one voice node (.muPattern body)
    // Warm the wavetable bank (dedup-by-path) for every user wavetable referenced
    // by a staged tree, so the boundary commit does no disk I/O. Lock-safe vs the
    // audio thread (bank append under voicesLock).
    void preloadWavetablesFromState(const juce::ValueTree& state);     // full: walk the voice <Slot>s
    void preloadWavetablesFromVoiceTree(const juce::ValueTree& voiceTree); // per-voice: o1/o2WtPath

    // Register mixer/FX param listeners + run an initial engine sync (JUCE
    // doesn't fire parameterChanged on construction or for unchanged values, so
    // we seed mixerEngine/fxChain explicitly here + after a preset load).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
