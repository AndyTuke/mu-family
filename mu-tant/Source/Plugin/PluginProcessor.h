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

    // ── ProcessorBase channel metadata ───────────────────────────────────────
    // mu-tant always exposes the full 8 channels — voices are always present;
    // the user mutes / lowers what they don't want via the mixer.
    int          getNumChannels()              const override { return kMaxVoices; }
    juce::String getChannelName(int idx)       const override
    {
        return (idx >= 0 && idx < kMaxVoices) ? juce::String("Voice ") + juce::String(idx + 1)
                                              : juce::String();
    }
    int          getChannelColourIndex(int idx) const override { return idx; }

    // ── ProcessorBase preset wiring (per design-voice.md file formats) ────────
    juce::File   getPerSlotPresetDir()       const override { return {}; }   // TODO: content dir
    juce::String getPerSlotPresetExtension() const override { return "muPattern"; }
    juce::File   getFullPresetDir()          const override { return {}; }   // TODO: content dir
    juce::String getFullPresetExtension()    const override { return "muTant"; }

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

    // Internal free-running beat counter — mu-tant is a drone synth with no
    // transport, so modulators evaluate against an always-incrementing beat
    // position derived from sample rate + a fixed 120 BPM. Sized in beats
    // (quarter notes) since ControlSequence::evaluate takes a beat position.
    double internalBeatPos = 0.0;
    double internalBpm     = 120.0;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace mu_tant
