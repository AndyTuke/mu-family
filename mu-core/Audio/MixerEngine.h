#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include "InsertProcessor.h"
#include "VoiceParams.h"
#include "MuLimits.h"

class VoiceEngine;
class FXChain;

// Stage 34 Step 2: descriptor for the optional retired-engine render path.
// `engines` is a row-major MaxRhythms × perSlot matrix of unique_ptrs (passed as
// a raw pointer to element [0][0] so we don't template MixerEngine on the array
// shape). `cleanupFlags` is the parallel array of atomic flags — set by the
// audio thread when the corresponding engine reports isFullyDrained(), polled +
// cleared by the message thread under suspendProcessing for off-RT destruction.
// perSlot == 0 (or engines == nullptr) skips the retired-render loop entirely.
struct RetiredVoices
{
    std::unique_ptr<VoiceEngine>* engines      = nullptr;
    std::atomic<bool>*            cleanupFlags = nullptr;
    int                           perSlot      = 0;
};

// Per-channel gain, pan, mute, solo, FX sends, and VU metering.
// processBlock replaces the raw voice-accumulation loop in PluginProcessor.
class MixerEngine
{
public:
    // One mixer channel per layer — must equal mu_limits::kMaxChannels.
    static constexpr int MaxChannels = mu_limits::kMaxChannels;

    // Optional per-channel render hook. When supplied, Phase 1 invokes it to fill
    // channel r's buffer (channel index, target buffer, sample count) INSTEAD of
    // calling voices[r]->process — lets a product run its own engine→insert chain
    // into the channel buffer before the shared trim/sidechain/fader/pan/sends/
    // master path. Null → render from `voices` as before (mu-clid).
    using RenderChannelFn = std::function<void(int, juce::AudioBuffer<float>&, int)>;

    // Stage 20: fixed −6 dB pre-fader trim on every channel input. Absorbs the
    // worst-case +18 dB of summing across 8 simultaneous correlated voices and
    // leaves the master with healthy headroom even on hot, normalised samples.
    // Trim is invisible to the user — it sits between the voice output and the
    // (visible) channel fader, so all FX sends and post-fader peaks see the
    // attenuated signal automatically. Chosen empirically: existing presets
    // load 6 dB quieter, but a fresh project with a 0 dBFS sample now peaks
    // around −14 dBFS at the master output (≈ +4 dB on the calibrated VU).
    static constexpr float kHeadroomTrim = 0.5f;

    // External DAW sidechain: value one beyond the max channel index (8).
    // Set sidechainSource to this value to duck from the DAW sidechain input bus
    // instead of an internal mixer channel. The bus pointers are supplied via
    // setExternalSidechain() before each processBlock call.
    static constexpr int kExtSidechainSrc = MaxChannels;

    // Per-channel and per-return state.  All fields are atomics so they can be
    // written by the message thread (via syncChannelStripParam / parameterChanged)
    // and read by the audio thread (processBlock) without a data race.
    // Relaxed ordering suffices: each field is independent, and the audio thread
    // reads a consistent value for the current block — it does not need an ordering
    // guarantee relative to other fields.
    struct ChannelState
    {
        std::atomic<float> level      { 1.0f };   // 0–1 linear fader (0 dB default)
        std::atomic<float> pan        { 0.0f };   // -1 (L) … +1 (R)
        std::atomic<float> sendEffect { 0.0f };
        std::atomic<float> sendDelay  { 0.0f };
        std::atomic<float> sendReverb { 0.0f };
        std::atomic<bool>  mute       { false };
        std::atomic<bool>  solo       { false };
        // Sidechain ducking
        std::atomic<int>   sidechainSource   { -1 };     // -1=off, 0-7=channel index, kExtSidechainSrc=DAW bus
        std::atomic<float> sidechainAmount   { 0.0f };   // 0-1 ducking depth
        std::atomic<float> sidechainAttackMs  { 5.0f };
        std::atomic<float> sidechainReleaseMs { 100.0f };
        // Multi-bus output routing (DAW only): 0 = Master mix (default, applies master fader + FX),
        // 1..8 = direct out to Bus 1..8 (post-channel-fader, no master fader, no FX sends).
        std::atomic<int>   outputBus { 0 };

        // Copy all fields from another ChannelState. Use instead of `= other`
        // since atomics are not copy-assignable.
        void copyFrom(const ChannelState& o) noexcept
        {
            level.store(o.level.load(std::memory_order_relaxed), std::memory_order_relaxed);
            pan.store(o.pan.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendEffect.store(o.sendEffect.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendDelay.store(o.sendDelay.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendReverb.store(o.sendReverb.load(std::memory_order_relaxed), std::memory_order_relaxed);
            mute.store(o.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
            solo.store(o.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainSource.store(o.sidechainSource.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainAmount.store(o.sidechainAmount.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainAttackMs.store(o.sidechainAttackMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainReleaseMs.store(o.sidechainReleaseMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            outputBus.store(o.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        // Reset all fields to their default values. Use instead of `= ChannelState{}`
        // since atomics are not copy-assignable.
        void reset() noexcept
        {
            level.store(1.0f, std::memory_order_relaxed);
            pan.store(0.0f, std::memory_order_relaxed);
            sendEffect.store(0.0f, std::memory_order_relaxed);
            sendDelay.store(0.0f, std::memory_order_relaxed);
            sendReverb.store(0.0f, std::memory_order_relaxed);
            mute.store(false, std::memory_order_relaxed);
            solo.store(false, std::memory_order_relaxed);
            sidechainSource.store(-1, std::memory_order_relaxed);
            sidechainAmount.store(0.0f, std::memory_order_relaxed);
            sidechainAttackMs.store(5.0f, std::memory_order_relaxed);
            sidechainReleaseMs.store(100.0f, std::memory_order_relaxed);
            outputBus.store(0, std::memory_order_relaxed);
        }
    };

    struct ReturnState
    {
        std::atomic<float> level { 0.75f };
        std::atomic<float> pan   { 0.0f };
        std::atomic<bool>  mute  { false };
        std::atomic<bool>  solo  { false };
        // Sidechain ducking (source = channel index, -1=off, kExtSidechainSrc=DAW bus)
        std::atomic<int>   sidechainSource   { -1 };
        std::atomic<float> sidechainAmount   { 0.0f };
        std::atomic<float> sidechainAttackMs  { 5.0f };
        std::atomic<float> sidechainReleaseMs { 100.0f };
    };

    std::array<ChannelState, MaxChannels> channels;
    std::array<ReturnState,  3>           returns;   // 0=effect, 1=delay, 2=reverb
    std::atomic<float> masterLevel { 1.0f };   // 0 dB default
    std::atomic<float> masterPan   { 0.0f };

    // Master insert effects — Insert 1 → Insert 2 → master output.
    // Written by the message thread via syncMasterParam; read by the audio thread
    // in processBlock. Each field is individually atomic so concurrent reads and
    // writes never produce a torn value.
    std::atomic<int>   masterInsert1Algo    { 0 };
    std::atomic<float> masterInsert1Param[VoiceParams::kInsertSlotCount] {};
    std::atomic<int>   masterInsert2Algo    { 0 };
    std::atomic<float> masterInsert2Param[VoiceParams::kInsertSlotCount] {};
    InsertProcessor    masterInsert;
    InsertProcessor    masterInsert2;

    // Peak levels written from the audio thread, read by the UI at 30 Hz.
    std::atomic<float> channelPeaks[MaxChannels];
    std::atomic<float> returnPeaks[3];
    std::atomic<float> masterPeak;
    // Per-channel peak gain-reduction (0 = no duck, 1 = full duck), written each block.
    std::atomic<float> sidechainGR[MaxChannels];
    // Per-return peak gain-reduction for sidechain ducking.
    std::atomic<float> returnSidechainGR[3];

    MixerEngine();

    void prepare(double sampleRate, int blockSize);

    // UI helper: assemble a VoiceParams snapshot from the master-insert atomics.
    // slot 0 = Insert 1, slot 1 = Insert 2. Called from the message thread.
    VoiceParams getMasterInsertParams(int slot) const noexcept
    {
        VoiceParams vp;
        const auto& algo   = (slot == 0) ? masterInsert1Algo   : masterInsert2Algo;
        const auto* params = (slot == 0) ? masterInsert1Param  : masterInsert2Param;
        vp.insertAlgo = algo.load(std::memory_order_relaxed);
        for (int i = 0; i < VoiceParams::kInsertSlotCount; ++i)
            vp.insertParam[i] = params[i].load(std::memory_order_relaxed);
        return vp;
    }


    // Clears output, accumulates per-channel audio with mixing applied, then runs fxChain.
    // directOuts[N] (if non-null) receives a copy of channel r's post-fader audio when
    // channel r's outputBus == N+1 (i.e. the channel is routed to "Out N+1" instead of
    // the master mix). Channels routed to direct outs skip FX sends and the master fader.
    // fxReturnsOut (if non-null) receives a copy of the post-fader FX return mix.
    void processBlock(juce::AudioBuffer<float>&   output,
                      int                         numActiveChannels,
                      std::unique_ptr<VoiceEngine>* voices,
                      FXChain&                    fxChain,
                      int                         numSamples,
                      std::array<juce::AudioBuffer<float>*, MaxChannels>* directOuts = nullptr,
                      juce::AudioBuffer<float>*    fxReturnsOut = nullptr,
                      const RetiredVoices*        retired      = nullptr,
                      const RenderChannelFn*      renderChannel = nullptr);

    // Swap two ChannelState entries in place. std::swap cannot be used because
    // std::atomic<T> is not moveable; this does a field-by-field load/store swap.
    // Called on the message thread during rhythm reorder; the audio thread is
    // quiescent (suspendProcessing) at that point so no race.
    void swapChannelState(int i, int j) noexcept
    {
        if (i == j) return;
        auto& a = channels[(size_t)i];
        auto& b = channels[(size_t)j];
        auto swapF = [](std::atomic<float>& x, std::atomic<float>& y) noexcept {
            const float t = x.load(std::memory_order_relaxed);
            x.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
            y.store(t, std::memory_order_relaxed);
        };
        auto swapB = [](std::atomic<bool>& x, std::atomic<bool>& y) noexcept {
            const bool t = x.load(std::memory_order_relaxed);
            x.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
            y.store(t, std::memory_order_relaxed);
        };
        auto swapI = [](std::atomic<int>& x, std::atomic<int>& y) noexcept {
            const int t = x.load(std::memory_order_relaxed);
            x.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
            y.store(t, std::memory_order_relaxed);
        };
        swapF(a.level,              b.level);
        swapF(a.pan,                b.pan);
        swapF(a.sendEffect,         b.sendEffect);
        swapF(a.sendDelay,          b.sendDelay);
        swapF(a.sendReverb,         b.sendReverb);
        swapB(a.mute,               b.mute);
        swapB(a.solo,               b.solo);
        swapI(a.sidechainSource,    b.sidechainSource);
        swapF(a.sidechainAmount,    b.sidechainAmount);
        swapF(a.sidechainAttackMs,  b.sidechainAttackMs);
        swapF(a.sidechainReleaseMs, b.sidechainReleaseMs);
        swapI(a.outputBus,          b.outputBus);
    }

    // Reset the sidechain envelope follower state for one channel slot. Called
    // by PluginProcessor::swapRhythms so the previous tenant's ducking envelope
    // does not bleed into the freshly arrived rhythm.
    void resetSidechainEnv(int channelIndex) noexcept
    {
        if (channelIndex >= 0 && channelIndex < MaxChannels)
            scEnv[channelIndex] = 0.0f;
    }

    // Supply the DAW sidechain input bus pointers for the upcoming processBlock call.
    // Call with (nullptr, nullptr) when the bus is inactive. Pointers must remain
    // valid for the duration of processBlock — they are raw views into the host buffer.
    void setExternalSidechain(const float* L, const float* R) noexcept
    {
        extScL = L;
        extScR = (R != nullptr) ? R : L;   // mono sidechain: fold to both channels
    }

    // Copy the DAW sidechain input into a private buffer and point the follower at it.
    // Use this (not setExternalSidechain) when the host's source channels would be
    // overwritten before processBlock reads them — e.g. an instrument whose sidechain
    // input bus shares buffer channels with its main output, which the buffer.clear()
    // at the top of processBlock wipes. The copy is sized in prepare(), so this never
    // allocates on the audio thread.
    void copyExternalSidechain(const juce::AudioBuffer<float>& scBuf) noexcept
    {
        const int nCh = juce::jmin(scBuf.getNumChannels(), extScCapture.getNumChannels());
        const int n   = juce::jmin(scBuf.getNumSamples(),  extScCapture.getNumSamples());
        if (nCh < 1 || n < 1) { extScL = extScR = nullptr; return; }
        for (int c = 0; c < nCh; ++c)
            extScCapture.copyFrom(c, 0, scBuf, c, 0, n);
        extScL = extScCapture.getReadPointer(0);
        extScR = (nCh >= 2) ? extScCapture.getReadPointer(1) : extScL;
    }

private:
    double sampleRate = 44100.0;
    float  scEnv[MaxChannels] {};     // per-channel sidechain envelope state
    float  scRetEnv[3] {};            // per-return sidechain envelope state

    // External DAW sidechain bus — set by setExternalSidechain() before processBlock,
    // or copied into extScCapture by copyExternalSidechain() when the host channels
    // would be clobbered by the product's buffer.clear() (instrument SC/out overlap).
    const float* extScL = nullptr;
    const float* extScR = nullptr;
    juce::AudioBuffer<float> extScCapture;   // private copy that survives buffer.clear()

    juce::AudioBuffer<float> channelBufs[MaxChannels];
    juce::AudioBuffer<float> effectSendBuf, delaySendBuf, reverbSendBuf;

    bool         hasSolo(int numActive) const;
    static float peakOf(const juce::AudioBuffer<float>& buf, int numSamples);
    static bool  hasSignal(const juce::AudioBuffer<float>& buf, int numSamples);
    static void  applyPanGain(juce::AudioBuffer<float>& buf,
                              float level, float pan, int numSamples);
};
