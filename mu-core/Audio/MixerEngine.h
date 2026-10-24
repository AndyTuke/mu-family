#pragma once
#include <cmath>
#include <limits>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include "InsertProcessor.h"
#include "VoiceParams.h"
#include "MuLimits.h"
#include "SidechainRemap.h"

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
    // One mixer channel per layer — must equal mu_limits::kMaxLayers.
    static constexpr int MaxChannels = mu_limits::kMaxLayers;

    // Optional per-channel render hook. When supplied, Phase 1 invokes it to fill
    // channel r's buffer (channel index, target buffer, sample count) INSTEAD of
    // calling voices[r]->process — lets a product run its own engine→insert chain
    // into the channel buffer before the shared trim/sidechain/fader/pan/sends/
    // master path. Null → render from `voices` as before (mu-clid).
    using RenderChannelFn = std::function<void(int, juce::AudioBuffer<float>&, int)>;

    // TEST-ONLY tap (approved for the layer-extraction equivalence test, E0.9): when set, Phase 1 hands it
    // each channel's buffer exactly as the channel gives it to the mixer (the render hook or engine output
    // plus any retired tail, before the headroom trim). It must not allocate or block: it runs on the audio
    // thread. Null in every product; set only by tests, while the processor is not processing.
    struct ChannelTap
    {
        virtual ~ChannelTap() = default;
        virtual void capture(int channel, const juce::AudioBuffer<float>& buf, int numSamples) noexcept = 0;
    };
    ChannelTap* channelTap = nullptr;

    // Stage 20: fixed −6 dB pre-fader trim on every channel input. Absorbs the
    // worst-case +18 dB of summing across 8 simultaneous correlated voices and
    // leaves the master with healthy headroom even on hot, normalised samples.
    // Trim is invisible to the user — it sits between the voice output and the
    // (visible) channel fader, so all FX sends and post-fader peaks see the
    // attenuated signal automatically. Chosen empirically: existing presets
    // load 6 dB quieter, but a fresh project with a 0 dBFS sample now peaks
    // around −14 dBFS at the master output (≈ +4 dB on the calibrated VU).
    static constexpr float kHeadroomTrim = 0.5f;

    // Group buses (mu_limits::kMaxBuses, 0 in a product without them) and the inserts on each.
    static constexpr int kNumBuses   = mu_limits::kMaxBuses;
    static constexpr int kBusInserts = mu_limits::kBusInserts;
    // Bus and insert indices are one digit in the parameter ids (bus{B}_i{I}_), which ProcessorBase parses.
    static_assert(kNumBuses <= 9 && kBusInserts <= 9, "bus parameter ids carry one-digit bus and insert indices");
    static_assert(kNumBuses >= 0 && kBusInserts >= 1, "MU_MAX_BUSES must be >= 0 and MU_BUS_INSERTS >= 1");

    // Sidechain source encoding (a plain int): -1 = off, 0..MaxChannels-1 = a channel, kExtSidechainSrc =
    // the DAW sidechain input (bus pointers supplied via setExternalSidechain() before each processBlock
    // call), kExtSidechainSrc + 1 + b = group bus b. See SidechainRemap.h for which strip may take which.
    static constexpr int kExtSidechainSrc = MaxChannels;
    static constexpr int busSidechainSource(int bus) noexcept { return mu_mix::busSource(bus, MaxChannels); }

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
        // Modulated pan / sends for the current block, written by the product's modulation pass
        // (kNoMod = not modulated: use the values above). Never saved — the parameters keep the
        // knob positions.
        static constexpr float kNoMod = std::numeric_limits<float>::quiet_NaN();
        std::atomic<float> panMod        { kNoMod };
        std::atomic<float> sendEffectMod { kNoMod };
        std::atomic<float> sendDelayMod  { kNoMod };
        std::atomic<float> sendReverbMod { kNoMod };
        // The value to use this block: the modulated one when set, else the knob's.
        static float effective(const std::atomic<float>& base, const std::atomic<float>& mod) noexcept
        {
            const float m = mod.load(std::memory_order_relaxed);
            return std::isnan(m) ? base.load(std::memory_order_relaxed) : m;
        }
        std::atomic<bool>  mute       { false };
        std::atomic<bool>  solo       { false };
        // Sidechain ducking
        std::atomic<int>   sidechainSource   { -1 };     // -1=off, 0-7=channel index, kExtSidechainSrc=DAW bus
        std::atomic<float> sidechainAmount   { 0.0f };   // 0-1 ducking depth
        std::atomic<float> sidechainAttackMs  { 0.0f };
        std::atomic<float> sidechainReleaseMs { 0.0f };
        // Multi-bus output routing (DAW only): 0 = Master mix (default, applies master fader + FX),
        // 1..8 = direct out to Bus 1..8 (post-channel-fader, no master fader, no FX sends).
        std::atomic<int>   outputBus { 0 };
        // Group bus this channel's dry signal feeds: 0 = master, 1..kNumBuses = bus 1.... A non-zero
        // outputBus (a DAW direct out) wins: that channel feeds neither a bus nor the master. Never used
        // where kNumBuses is 0. Part of the strip, so it moves with it; not in a layer preset or clip.
        std::atomic<int>   busAssign { 0 };

        // Copy all fields from another ChannelState. Use instead of `= other`
        // since atomics are not copy-assignable.
        void copyFrom(const ChannelState& o) noexcept
        {
            level.store(o.level.load(std::memory_order_relaxed), std::memory_order_relaxed);
            pan.store(o.pan.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendEffect.store(o.sendEffect.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendDelay.store(o.sendDelay.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendReverb.store(o.sendReverb.load(std::memory_order_relaxed), std::memory_order_relaxed);
            panMod.store(o.panMod.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendEffectMod.store(o.sendEffectMod.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendDelayMod.store(o.sendDelayMod.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sendReverbMod.store(o.sendReverbMod.load(std::memory_order_relaxed), std::memory_order_relaxed);
            mute.store(o.mute.load(std::memory_order_relaxed), std::memory_order_relaxed);
            solo.store(o.solo.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainSource.store(o.sidechainSource.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainAmount.store(o.sidechainAmount.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainAttackMs.store(o.sidechainAttackMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            sidechainReleaseMs.store(o.sidechainReleaseMs.load(std::memory_order_relaxed), std::memory_order_relaxed);
            outputBus.store(o.outputBus.load(std::memory_order_relaxed), std::memory_order_relaxed);
            busAssign.store(o.busAssign.load(std::memory_order_relaxed), std::memory_order_relaxed);
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
            for (auto* m : { &panMod, &sendEffectMod, &sendDelayMod, &sendReverbMod })
                m->store(kNoMod, std::memory_order_relaxed);
            mute.store(false, std::memory_order_relaxed);
            solo.store(false, std::memory_order_relaxed);
            sidechainSource.store(-1, std::memory_order_relaxed);
            sidechainAmount.store(0.0f, std::memory_order_relaxed);
            sidechainAttackMs.store(0.0f, std::memory_order_relaxed);
            sidechainReleaseMs.store(0.0f, std::memory_order_relaxed);
            outputBus.store(0, std::memory_order_relaxed);
            busAssign.store(0, std::memory_order_relaxed);
        }
    };

    // One insert slot on a bus: the layer's insert (algorithm, four generic parameters, the processor)
    // under atomics the message thread writes and the audio thread reads once per block.
    struct BusInsert
    {
        std::atomic<int>   insertAlgo { 0 };
        std::atomic<float> insertParam[VoiceParams::kInsertSlotCount] {};
        InsertProcessor    processor;

        // The block's parameter snapshot, as the master inserts build theirs.
        VoiceParams snapshot() const noexcept
        {
            VoiceParams vp;
            vp.insertAlgo = insertAlgo.load(std::memory_order_relaxed);
            for (int i = 0; i < VoiceParams::kInsertSlotCount; ++i)
                vp.insertParam[i] = insertParam[i].load(std::memory_order_relaxed);
            return vp;
        }
    };

    // A group bus: a channel strip's fields (level, pan, mute, solo, three sends, sidechain; outputBus and
    // busAssign unused: a bus goes to the master) plus its insert chain, insert 0 then 1.
    struct BusState : ChannelState
    {
        BusInsert insert[kBusInserts];
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
        std::atomic<float> sidechainAttackMs  { 0.0f };
        std::atomic<float> sidechainReleaseMs { 0.0f };
    };

    std::array<ChannelState, MaxChannels> channels;
    std::array<ReturnState,  3>           returns;   // 0=effect, 1=delay, 2=reverb
    std::array<BusState, (size_t) kNumBuses> buses;   // empty where kNumBuses is 0
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
    // The combined peak (larger of left and right) plus the left / right peaks behind the stereo VU
    // meters, all post-fader and post-pan.
    std::atomic<float> channelPeaks[MaxChannels];
    std::atomic<float> returnPeaks[3];
    std::atomic<float> masterPeak;
    std::atomic<float> channelPeaksL[MaxChannels], channelPeaksR[MaxChannels];
    std::atomic<float> returnPeaksL[3], returnPeaksR[3];
    std::atomic<float> masterPeakL, masterPeakR;
    // Per-channel peak gain-reduction (0 = no duck, 1 = full duck), written each block.
    std::atomic<float> sidechainGR[MaxChannels];
    // Per-return peak gain-reduction for sidechain ducking.
    std::atomic<float> returnSidechainGR[3];
    // Bus peaks (post-fader, post-pan; combined, left, right) and the bus sidechain gain-reduction. A bus
    // insert's own Compressor / Limiter gain reduction is on buses[b].insert[i].processor.grReduction.
    std::array<std::atomic<float>, (size_t) kNumBuses> busPeaks, busPeaksL, busPeaksR, busSidechainGR;
    // How many blocks were silenced because the master output held a NaN or infinity (the FX tails and master
    // inserts were reset with them). Written by the audio thread; read by tests and diagnostics.
    std::atomic<int> nonFiniteBlocks { 0 };

    MixerEngine();

    void prepare(double sampleRate, int blockSize);
    // The block size prepare() sized the channel and send buffers for: the longest block processBlock may get.
    int getPreparedBlockSize() const noexcept { return channelBufs[0].getNumSamples(); }

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
        swapF(a.panMod,             b.panMod);
        swapF(a.sendEffectMod,      b.sendEffectMod);
        swapF(a.sendDelayMod,       b.sendDelayMod);
        swapF(a.sendReverbMod,      b.sendReverbMod);
        swapB(a.mute,               b.mute);
        swapB(a.solo,               b.solo);
        swapI(a.sidechainSource,    b.sidechainSource);
        swapF(a.sidechainAmount,    b.sidechainAmount);
        swapF(a.sidechainAttackMs,  b.sidechainAttackMs);
        swapF(a.sidechainReleaseMs, b.sidechainReleaseMs);
        swapI(a.outputBus,          b.outputBus);
        swapI(a.busAssign,          b.busAssign);
        std::swap(scEnv[i], scEnv[j]);   // the ducking follower belongs to the strip and moves with it
        // The meters and the ducking read-out belong to the strip too, and so does a follower reset still waiting for
        // the audio thread (consumed at its next block start, so the bit is plain data while the audio thread is excluded).
        swapF(channelPeaks[i],  channelPeaks[j]);
        swapF(channelPeaksL[i], channelPeaksL[j]);
        swapF(channelPeaksR[i], channelPeaksR[j]);
        swapF(sidechainGR[i],   sidechainGR[j]);
        swapB(scEnvResetRequest[i], scEnvResetRequest[j]);
    }

    // A layer was removed at `removed` and the ones above are about to shift down: re-point every
    // sidechain source (channel strips, bus strips and the three return strips; a bus source never moves).
    // Call BEFORE the strips shift.
    void renumberSidechainsAfterRemove(int removed) noexcept
    {
        for (auto& c : channels)
            c.sidechainSource.store(mu_mix::sidechainAfterRemove(c.sidechainSource.load(std::memory_order_relaxed), removed, MaxChannels), std::memory_order_relaxed);
        for (auto& r : returns)
            r.sidechainSource.store(mu_mix::sidechainAfterRemove(r.sidechainSource.load(std::memory_order_relaxed), removed, MaxChannels), std::memory_order_relaxed);
        for (auto& b : buses)
            b.sidechainSource.store(mu_mix::sidechainAfterRemove(b.sidechainSource.load(std::memory_order_relaxed), removed, MaxChannels), std::memory_order_relaxed);
    }

    // Layers `a` and `b` are about to swap strips: re-point every sidechain source that named either.
    // Call BEFORE the strips swap.
    void renumberSidechainsAfterSwap(int a, int b) noexcept
    {
        for (auto& c : channels)
            c.sidechainSource.store(mu_mix::sidechainAfterSwap(c.sidechainSource.load(std::memory_order_relaxed), a, b, MaxChannels), std::memory_order_relaxed);
        for (auto& r : returns)
            r.sidechainSource.store(mu_mix::sidechainAfterSwap(r.sidechainSource.load(std::memory_order_relaxed), a, b, MaxChannels), std::memory_order_relaxed);
        for (auto& bus : buses)
            bus.sidechainSource.store(mu_mix::sidechainAfterSwap(bus.sidechainSource.load(std::memory_order_relaxed), a, b, MaxChannels), std::memory_order_relaxed);
    }

    // Read / seed one strip's follower (state inspection for the layer-edit tests only: never call
    // these while audio is running, the follower is plain audio-thread state).
    float getSidechainEnv(int channelIndex) const noexcept
    {
        jassert(channelIndex >= 0 && channelIndex < MaxChannels);
        return scEnv[channelIndex];
    }
    void setSidechainEnv(int channelIndex, float v) noexcept
    {
        jassert(channelIndex >= 0 && channelIndex < MaxChannels);
        scEnv[channelIndex] = v;
    }

    // Carry one strip's sidechain follower to another slot (a remove shifts the strips down).
    void moveSidechainEnv(int from, int to) noexcept { scEnv[to] = scEnv[from]; }

    // Reset the sidechain envelope follower for one channel slot: used when a slot gets a fresh strip
    // (add, reset, the vacated top slot after a remove), so no old ducking envelope carries over.
    void resetSidechainEnv(int channelIndex) noexcept
    {
        if (channelIndex >= 0 && channelIndex < MaxChannels)
            scEnv[channelIndex] = 0.0f;
    }

    // Ask for one strip's follower to be zeroed. Any thread: the audio thread consumes the request at the start
    // of its next block, so the follower (plain audio-thread state) is never written from the message thread
    // while a block runs. Used when a slot's layer type changes under a running host.
    void requestSidechainEnvReset(int channelIndex) noexcept
    {
        if (channelIndex >= 0 && channelIndex < MaxChannels)
            scEnvResetRequest[channelIndex].store(true, std::memory_order_release);
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
    std::atomic<bool> scEnvResetRequest[MaxChannels] {};   // set by requestSidechainEnvReset, cleared by the next block
    float  scRetEnv[3] {};            // per-return sidechain envelope state
    float  scBusEnv[(size_t) (kNumBuses > 0 ? kNumBuses : 1)] {};   // per-bus sidechain envelope state

    // External DAW sidechain bus — set by setExternalSidechain() before processBlock,
    // or copied into extScCapture by copyExternalSidechain() when the host channels
    // would be clobbered by the product's buffer.clear() (instrument SC/out overlap).
    const float* extScL = nullptr;
    const float* extScR = nullptr;
    juce::AudioBuffer<float> extScCapture;   // private copy that survives buffer.clear()

    juce::AudioBuffer<float> channelBufs[MaxChannels];
    juce::AudioBuffer<float> effectSendBuf, delaySendBuf, reverbSendBuf;
    // The channel each return's sidechain listens to, copied before the strip's fader and pan are applied (as
    // the channel-to-channel ducking reads it), so the fader does not change how hard a return ducks.
    juce::AudioBuffer<float> returnScSource[3];
    // Group buses (sized in prepare, empty where there are none). busBufs sums the dry signal of each bus's
    // channels; busTap keeps a bus's signal after its inserts and before its fader for the strips that duck
    // from it; busScSource holds the channel copy a bus ducks from (taken before the faders, as for returns).
    std::array<juce::AudioBuffer<float>, (size_t) kNumBuses> busBufs, busTap, busScSource;

    static float peakOf(const juce::AudioBuffer<float>& buf, int numSamples);
    // Publish a buffer's combined, left and right peaks / silence them. A mono buffer reports its one
    // channel as both sides.
    static void storePeak(const juce::AudioBuffer<float>& buf, int numSamples,
                          std::atomic<float>& both, std::atomic<float>& left, std::atomic<float>& right);
    static void clearPeak(std::atomic<float>& both, std::atomic<float>& left, std::atomic<float>& right);
    static bool  hasSignal(const juce::AudioBuffer<float>& buf, int numSamples);
    static void  applyPanGain(juce::AudioBuffer<float>& buf,
                              float level, float pan, int numSamples);
};
