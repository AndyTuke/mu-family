#pragma once

#include "Plugin/ProcessorBase.h"            // mu-core base
#include "Sequencer/VoiceSlot.h"             // mu-core: per-voice modulator data container
#include "Sequencer/GatePattern.h"           // mu-tant: per-voice gate pattern
#include "Audio/SynthVoice.h"                // mu-tant voice
#include "Audio/WavetableBank.h"

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <unordered_map>

// mu-tant — wavetable drone synth.
//
// Stage A1: expanded to 8 free-running voices. Each voice has its own
// oscillators / cross-mod / filter / level state, exposed in APVTS under
// per-voice subtree IDs `v{N}_*` (osc1/osc2/xmod/mix/filter/level). Shared
// tonal centre (`root`, `scale`) stays global. processBlock sums voices
// directly; the mixer's per-channel level / pan / mute / solo is applied
// from `mixerEngine.channels[]` (FX sends + sidechain pending the
// MixerEngine voice-render-callback refactor).
namespace mu_tant
{

class PluginProcessor : public ProcessorBase
{
public:
    // Family parity with mu-clid (max 8 rhythms / 8 voices / 8 channels).
    static constexpr int kMaxVoices = 8;

    PluginProcessor();
    ~PluginProcessor() override = default;

    // ── AudioProcessor ───────────────────────────────────────────────────────
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "mu-Tant"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
        if (!now) internalBeatPos.store(0.0, std::memory_order_relaxed);
    }
    double getInternalBpm()     const override { return internalBpm; }
    void   setInternalBpm(double bpm) override { internalBpm = juce::jlimit(20.0, 300.0, bpm); }
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
    int          getChannelColourIndex(int idx) const override { return idx; }

    // ── Dynamic voice management (message-thread; mirrors mu-clid add/delete) ──
    // addVoice appends a fresh default voice (returns its index, or -1 if full).
    // removeVoice deletes a voice, shifting every higher voice's APVTS values +
    // gate/modulator data down so the set stays contiguous. The last voice can't
    // be removed. Both hold `voicesLock`; processBlock tryLocks it.
    int  getNumVoices() const noexcept { return numVoices.load(std::memory_order_relaxed); }
    int  addVoice();
    void removeVoice(int idx);
    void swapVoices(int a, int b);   // reorder (drag in the sidebar)

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getContentDir()             const override;
    juce::File   getPresetsDir()             const override;   // full presets live here
    juce::File   getPerSlotPresetDir()       const override;   // voice presets live here
    juce::String getPerSlotPresetExtension() const override { return "muPattern"; }
    juce::File   getFullPresetDir()          const override { return getPresetsDir(); }
    juce::String getFullPresetExtension()    const override { return "muTant"; }

    // Full-preset save/load — the editor shell drives the UI (TransportBar
    // dropdown + Save dialog + Preset browser) and calls these. A preset is the
    // whole APVTS state wrapped with name/description/category metadata.
    void              savePreset(const juce::String& name, const juce::String& desc,
                                 const juce::String& category, bool embedSamples) override;
    void              loadPreset(const juce::File& file) override;
    juce::StringArray loadCategoryList() const override;

    // ── Per-voice param IDs (used by VoicePanel for SliderAttachment binding) ──
    // Family rule: per-voice params are subtree-scoped via `v{N}_` prefix so a
    // ValueTree restore round-trips cleanly + presets can target a specific
    // voice without name collisions across the 8 slots.
    static juce::String voiceParamId(int voice, const juce::String& base)
    {
        return juce::String("v") + juce::String(voice) + "_" + base;
    }

protected:
    // MIDI program-change apply hooks — stubbed in the first stab (no preset I/O yet).
    void applyMidiPresetSlot(int, const juce::File&) override {}
    void applyFullMidiPreset(const juce::File&)      override {}

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    VoiceConfig readConfig(int voiceIdx) const;

    WavetableBank                                          bank;
    std::array<std::unique_ptr<VoiceEngine>, kMaxVoices>   voices;
    // Per-voice render scratch — allocated in prepareToPlay; reused each block.
    std::array<juce::AudioBuffer<float>,     kMaxVoices>   voiceBuffers;

public:
    // Per-voice modulator data — 8 ControlSequences + ModulationMatrix + modLock
    // per voice. Public so the UI (ModulatorPanel) can pass a pointer to the
    // currently-edited voice's slot.
    std::array<VoiceSlot, kMaxVoices> voiceSlots;

    // Per-voice drawable gate pattern. Public so the (future) GatePatternEditor
    // can mutate it under voiceSlots[v].modLock. Currently empty — audio path
    // doesn't yet apply the gate.
    std::array<GatePattern, kMaxVoices> gatePatterns;

private:
    // Pre-allocated modulation paramValues map — reused every block to avoid
    // audio-thread allocation. Keys match the strings in MuTantModDest::kModDestTable.
    // Values are seeded each block from the current VoiceConfig and read back
    // after the matrix runs.
    std::unordered_map<std::string_view, float> modParamValues;

    // Internal transport. `playing` gates the beat advance; `internalBeatPos`
    // is the song position in beats (quarter notes) that drives modulator
    // evaluation + the gate engine + the gating-grid playhead. Atomic because
    // the UI reads them at 30 Hz off the message thread while the audio thread
    // advances them. internalBpm is message-thread-only (set from the BPM box).
    std::atomic<bool>   playing { false };
    std::atomic<double> internalBeatPos { 0.0 };
    double internalBpm     = 120.0;
    double currentSampleRate = 44100.0;

    // Number of existing voices (layers), 1..kMaxVoices. Audio thread reads it
    // atomically; add/removeVoice mutate it on the message thread under voicesLock.
    std::atomic<int> numVoices { 1 };
    // Guards the voice count + the per-voice data shift during add/remove against
    // the audio thread. processBlock takes a ScopedTryLock and silences the block
    // on contention (a sub-millisecond gap while a voice is added/removed).
    juce::CriticalSection voicesLock;

    // Copy every `v{src}_*` and `ch{src}_*` APVTS parameter value to the matching
    // `v{dst}_*` / `ch{dst}_*` parameter (used by removeVoice's down-shift).
    void copyVoiceParams(int src, int dst);
    // Reset one voice slot to defaults — APVTS params + gate pattern + modulators.
    void resetVoiceSlot(int idx);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
