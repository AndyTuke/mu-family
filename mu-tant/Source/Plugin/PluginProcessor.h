#pragma once

#include "Plugin/ProcessorBase.h"            // mu-core base
#include "Plugin/MixerFxParams.h"            // mu-core: shared global-FX/mixer APVTS layout
#include "Sequencer/VoiceSlot.h"             // mu-core: per-voice modulator data container
#include "Sequencer/GatePattern.h"           // mu-tant: per-voice gate pattern
#include "Audio/SynthVoice.h"                // mu-tant voice
#include "Audio/WavetableBank.h"
#include "Audio/InsertProcessor.h"           // mu-core: shared per-voice insert FX

#include "Modulation/MuTantModSnap.h"
#include "Plugin/VoiceHotSwapStager.h"       // mu-tant: preset hot-swap staging

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

class PluginProcessor : public ProcessorBase,
                        public juce::AudioProcessorValueTreeState::Listener,
                        public juce::AsyncUpdater
{
public:
    // Family parity with mu-clid (max 8 rhythms / 8 voices / 8 channels).
    static constexpr int kMaxVoices = 8;

    PluginProcessor();
    ~PluginProcessor() override;

    // Mixer / FX params (channel strips + global FX) drive mixerEngine + fxChain
    // via the shared ProcessorBase::syncGlobalFxParam, kept in sync by this
    // listener (mirrors mu-clid). Voice-engine params (v{N}_*) are read per-block
    // via cached pointers instead, so they're not listened to here.
    void parameterChanged(const juce::String& id, float v) override;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

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

    // Message-thread: commit any hot-swap that reached its loop boundary, then
    // drain the MIDI program-change queue. Triggered from processBlock via
    // triggerAsyncUpdate() when the audio thread flags a boundary / a PC arrives.
    void handleAsyncUpdate() override;

    // ── Internal transport (drives the gate engine + the gating timeline) ─────
    // mu-tant has no host-sync sequencer; the transport bar's play button starts
    // / stops an internal free-running clock. While stopped the beat freezes at 0
    // and the gate is held fully open (audition the oscillators); while playing
    // the beat advances and the gate engine chops per the pattern.
    bool   isInternalPlaying()  const override { return playing.load(std::memory_order_relaxed); }
    void   toggleInternalPlay() override
    {
        const bool now = !playing.load(std::memory_order_relaxed);
        playing.store(now, std::memory_order_relaxed);
        if (!now)
        {
            // Reset beat position on stop so pattern loops restart cleanly.
            internalBeatPos.store(0.0, std::memory_order_relaxed);
        }
    }
    double getInternalBpm()     const override { return internalBpm.load(std::memory_order_relaxed); }
    void   setInternalBpm(double bpm) override { internalBpm.store(juce::jlimit(20.0, 300.0, bpm), std::memory_order_relaxed); }
    double getInternalBeatPos() const override { return internalBeatPos.load(std::memory_order_relaxed); }

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
    void resetVoice(int idx);        // reset a voice to defaults (keeps its colour)

    // Per-voice ("layer") presets — the voice's `v{N}_*` subtree saved/loaded as
    // a `.muPattern` file (voice-agnostic base IDs, so a preset loads into any
    // slot). Mirrors mu-clid's per-rhythm preset I/O.
    void saveVoicePreset(int voice, const juce::String& name);
    void loadVoicePreset(int voice, const juce::File& file);

    // ── User wavetable import (per oscillator) ───────────────────────────────
    // Load a Serum/Vital .wav into the shared bank (dedup by path) and point the
    // given voice's oscillator at it; clear reverts to the factory selection.
    void         loadUserWavetable(int voice, int oscIdx, const juce::File& file);
    void         clearUserWavetable(int voice, int oscIdx);
    juce::String userWavetablePath(int voice, int oscIdx) const;    // "" = factory selection
    bool         userWavetableMissing(int voice, int oscIdx) const; // path set but file gone

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getContentDir()             const override;
    juce::File   getPresetsDir()             const override;   // full presets live here
    juce::File   getPerSlotPresetDir()       const override;   // voice presets live here
    juce::String getPerSlotPresetExtension() const override { return "muPattern"; }
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muTant"; }
    juce::File   getWavetablesDir()          const;            // user/factory .wav wavetables live here

    // Full-preset save/load — the editor shell drives the UI (TransportBar
    // dropdown + Save dialog + Preset browser) and calls these. A preset is the
    // whole APVTS state wrapped with name/description/category metadata.
    void              savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool embedSamples) override;
    void              loadPreset(const juce::File& file) override;
    juce::StringArray loadCategoryList() const override;

    // Fired (message thread, from handleAsyncUpdate) after a per-voice hot-swap
    // commit finishes, so the editor can refresh that voice's panel + sidebar +
    // wavetable dropdowns (state the APVTS round-trip doesn't cover by itself).
    // Full-preset commits use ProcessorBase::onPresetSwapCommitted. The editor
    // MUST clear this in its destructor: the processor can outlive the editor
    // (DAW close-window-keep-plugin), and a swap firing into a dead editor is a UAF.
    std::function<void(int voice)> onVoiceHotSwapCommitted;

    // Persist the UI scale selection (Medium / Large) so it survives a plugin
    // close/reopen. Writes to appSettings before delegating to the base class.
    void setUiScale(float scale) override;

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
    float getTantSnap(int voice, int snapIdx) const noexcept
    {
        if (voice < 0 || voice >= kMaxVoices) return 0.0f;
        return voiceSnap[(size_t) voice][(size_t) snapIdx].load();
    }

protected:
    // MIDI program-change apply hooks (drained on the message thread). Ch 1-8 →
    // per-voice preset into the matching slot; Ch 9 → full preset. Both entry
    // points hot-swap (stage at the loop boundary when playing, apply immediately
    // when stopped).
    void applyMidiPresetSlot(int slot, const juce::File& f) override { loadVoicePreset(slot, f); }
    void applyFullMidiPreset(const juce::File& f)           override { loadPreset(f); }

private:
    VoiceConfig readConfig(int voiceIdx) const;

    // Cached APVTS atomic pointers. The pointers returned by getRawParameterValue
    // are stable for the processor's lifetime (replaceState changes values, not
    // the parameter objects), so we resolve them once in the constructor and read
    // them in processBlock — never rebuilding juce::String IDs on the audio thread.
    struct VoicePtrs
    {
        std::atomic<float> *o1Oct, *o1Semi, *o1Fine, *o1Pos, *o1Wt;
        std::atomic<float> *o2Oct, *o2Semi, *o2Fine, *o2Pos, *o2Wt;
        std::atomic<float> *xmodFm, *xmodAm, *xmodRing, *sync;
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
    void   renderVoice(int voiceIdx, juce::AudioBuffer<float>& buf, int numSamples);
    // renderVoice phases — called in order; each handles one concern.
    void   applyModulation    (int v, VoiceConfig& cfg);
    void   applyFilterEnvelope(int v, VoiceConfig& cfg);
    void   applyPitchEnvelope (int v, VoiceConfig& cfg);
    bool   blkPlaying        = false;
    double blkBeatStart      = 0.0;
    double blkBeatsPerSample = 0.0;

    WavetableBank                                          bank;
    std::array<std::unique_ptr<VoiceEngine>, kMaxVoices>   voices;
    // Per-voice insert effect (shared mu-core InsertProcessor) — runs after the
    // gate, before the pan/sum into the mixer (engine → insert → mixer, the
    // family-wide signal flow). Mirrors mu-clid's per-rhythm insert.
    std::array<InsertProcessor,              kMaxVoices>   inserts;

public:
    // Per-voice modulator data — 8 ControlSequences + ModulationMatrix + modLock
    // per voice. Public so the UI (ModulatorPanel) can pass a pointer to the
    // currently-edited voice's slot.
    std::array<VoiceSlot, kMaxVoices> voiceSlots;

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

    // Internal transport. `playing` gates the beat advance; `internalBeatPos`
    // is the song position in beats (quarter notes) that drives modulator
    // evaluation + the gate engine + the gating-grid playhead. All atomic: the UI
    // reads/writes them off the message thread while the audio thread reads them
    // (internalBpm is written by setInternalBpm from the BPM box and read every
    // block for beatsPerSample — relaxed is fine, each is a lone published value).
    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    std::atomic<double> internalBpm { 120.0 };
    // Written in prepareToPlay (host suspends the audio thread first) — no atomic needed.
    double currentSampleRate = 44100.0;

    // Counts completed 2-bar pattern loops (incremented at each wrap in processBlock).
    // Read by the audio thread in renderVoice to drive per-envelope loopN/loopM +
    // probability rules. Wraps safely at uint max — the modulo check in playsOnLoop
    // is stable across the wrap.
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
    std::array<std::atomic<int>, kMaxVoices> osc1UserIdx, osc2UserIdx;
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

    // Per-voice modulator (ControlSequences + ModulationMatrix) + gate-pattern
    // persistence. These live OUTSIDE the APVTS parameters (in voiceSlots /
    // gatePatterns), so they're serialised into a <VoiceData> child of the APVTS
    // state tree on save and restored on load — otherwise a saved patch silently
    // loses every modulator assignment + drawn gate envelope. write/read cover the
    // active voices for full-state + full-preset paths; the per-voice (.muPattern)
    // path serialises one voice's modulators + gate alongside its params.
    void writeVoiceDataToState();
    void readVoiceDataFromState();

    // ── Preset hot-swap (#880 full / #883 per-voice) ─────────────────────────
    // loadPreset / loadVoicePreset stage the parsed tree when the transport is
    // playing (commit at the loop boundary) and apply immediately when stopped.
    // The apply bodies are factored out so the boundary commit (handleAsyncUpdate)
    // and the immediate path share one code path.
    VoiceHotSwapStager hotSwapStager;
    // Previous block's play state (audio-thread only) — drives the playing→stopped
    // edge that commits a staged swap on stop.
    bool wasPlaying = false;
    void applyFullPresetTree (const juce::ValueTree& state);          // replaceState + voice data + FX
    void applyVoicePresetTree(int voice, const juce::ValueTree& tree); // .muPattern body, from a tree
    // Warm the wavetable bank (dedup-by-path) for every user wavetable referenced
    // by a staged tree, so the boundary commit does no disk I/O. Lock-safe vs the
    // audio thread (bank append under voicesLock).
    void preloadWavetablesFromState(const juce::ValueTree& state);     // full: walk <VoiceData>
    void preloadWavetablesFromVoiceTree(const juce::ValueTree& voiceTree); // per-voice: o1/o2WtPath

    // Register mixer/FX param listeners + run an initial engine sync (JUCE
    // doesn't fire parameterChanged on construction or for unchanged values, so
    // we seed mixerEngine/fxChain explicitly here + after a preset load).
    void registerFxListeners();
    void syncAllFxParams();

    // Persistent user settings (UI scale, future BPM, etc.) — stored next to
    // the content dir so settings survive plugin re-installs.
    std::unique_ptr<juce::PropertiesFile> appSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
