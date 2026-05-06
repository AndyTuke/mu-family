#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

class VoiceEngine;
class FXChain;

// Per-channel gain, pan, mute, solo, FX sends, and VU metering.
// processBlock replaces the raw voice-accumulation loop in PluginProcessor.
class MixerEngine
{
public:
    static constexpr int MaxChannels = 8;

    // Stage 20: fixed −6 dB pre-fader trim on every channel input. Absorbs the
    // worst-case +18 dB of summing across 8 simultaneous correlated voices and
    // leaves the master with healthy headroom even on hot, normalised samples.
    // Trim is invisible to the user — it sits between the voice output and the
    // (visible) channel fader, so all FX sends and post-fader peaks see the
    // attenuated signal automatically. Chosen empirically: existing presets
    // load 6 dB quieter, but a fresh project with a 0 dBFS sample now peaks
    // around −14 dBFS at the master output (≈ +4 dB on the calibrated VU).
    static constexpr float kHeadroomTrim = 0.5f;

    struct ChannelState
    {
        float level      = 0.75f;   // 0–1 linear fader
        float pan        = 0.0f;    // -1 (L) … +1 (R)
        float sendEffect = 0.0f;
        float sendDelay  = 0.0f;
        float sendReverb = 0.0f;
        bool  mute       = false;
        bool  solo       = false;
        // Sidechain ducking
        int   sidechainSource   = -1;     // -1 = off, 0-7 = source channel index
        float sidechainAmount   = 0.0f;   // 0-1 ducking depth
        float sidechainAttackMs  =   5.0f;
        float sidechainReleaseMs = 100.0f;
    };

    struct ReturnState
    {
        float level = 0.75f;
        float pan   = 0.0f;
        bool  mute  = false;
        bool  solo  = false;
    };

    std::array<ChannelState, MaxChannels> channels;
    std::array<ReturnState,  3>           returns;   // 0=effect, 1=delay, 2=reverb
    float masterLevel = 0.75f;
    float masterPan   = 0.0f;

    // Peak levels written from the audio thread, read by the UI at 30 Hz.
    juce::Atomic<float> channelPeaks[MaxChannels];
    juce::Atomic<float> returnPeaks[3];
    juce::Atomic<float> masterPeak;

    MixerEngine();

    void prepare(double sampleRate, int blockSize);


    // Clears output, accumulates per-channel audio with mixing applied, then runs fxChain.
    void processBlock(juce::AudioBuffer<float>&   output,
                      int                         numActiveRhythms,
                      std::unique_ptr<VoiceEngine>* voices,
                      FXChain&                    fxChain,
                      int                         numSamples);

private:
    double sampleRate = 44100.0;
    float  scEnv[MaxChannels] {};    // per-channel sidechain envelope state

    juce::AudioBuffer<float> channelBufs[MaxChannels];
    juce::AudioBuffer<float> effectSendBuf, delaySendBuf, reverbSendBuf;

    bool         hasSolo(int numActive) const;
    static float peakOf(const juce::AudioBuffer<float>& buf, int numSamples);
    static bool  hasSignal(const juce::AudioBuffer<float>& buf, int numSamples);
    static void  applyPanGain(juce::AudioBuffer<float>& buf,
                              float level, float pan, int numSamples);
};
