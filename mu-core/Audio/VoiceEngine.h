#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SamplePlayer.h"
#include "VoiceParams.h"
#include "MultiModeFilter.h"
#include "InsertProcessor.h"
#include "MuLimits.h"

#include <array>
#include <atomic>
#include <vector>

// Per-rhythm voice chain: SamplePlayer pool → Amp ADSR → Filter + Filter ADSR → level trim.
// Pitch ratio is derived from VoiceParams (octave/semitones/fine) plus a pitch envelope.
class VoiceEngine
{
public:
    static constexpr int MaxVoices = mu_limits::kMaxSamplerVoices;

    VoiceEngine();

    void prepareToPlay(double sampleRate, int blockSize);
    void loadFile(const juce::File& file);
    void clearSample();
    // `tied` (pattern-legato) — when true, skip the envelope retrigger
    // entirely (env state continues uninterrupted across contiguous hits) and
    // ask SamplePlayer for a longer fade-in ramp so the sample-voice restart
    // doesn't click against the running envelope tail. The sample voice is
    // claimed and (re)started normally — the rhythmic pulse stays.
    void trigger(bool isAccented = false, bool tied = false);
    void process(juce::AudioBuffer<float>& output, int numSamples);
    bool hasSample() const;

    // Called from message thread. Stores params; audio thread picks them up in process().
    void setParams(const VoiceParams& p);

    // Called from audio thread only (PluginProcessor::processBlock).
    // Applies a fully-computed modulated VoiceParams for this block, bypassing the
    // message-thread pending mechanism.  Must be called before process().
    void setActiveParams(const VoiceParams& p);

    // Stage 34 Step 1: True when no sample voice is mid-playback (or pending trigger)
    // AND the amp envelope is in JUCE's idle state. A retired VoiceEngine that
    // returns true has nothing left to render and is safe to destroy off the audio
    // thread. Pure inspection — no state mutation, no side effects. Audio thread
    // calls this each block on retired engines; message thread polls a cleanup flag
    // set by the audio thread once this returns true.
    bool isFullyDrained() const noexcept;

    // Stage 34 Step 3 fix: prepare the engine for tail-out as a retired (frozen)
    // engine. Must be called by the message thread under suspendProcessing — no
    // audio-thread reads of the affected state can race with these mutations.
    //
    // Three things happen:
    //   1. noteOff() on all three envelopes so the amp env actually progresses
    //      to idle. Without this, the engine sits in the sustain state forever
    //      (no triggers / no noteOffs anywhere else in the codebase) and
    //      isFullyDrained() never returns true — the retired slot leaks and
    //      the engine renders until plugin destruction.
    //   2. voiceFilter.reset() + insertProc.reset() to clear comb-buffer /
    //      compressor-envelope / DC-block / bitrate-hold state. Otherwise a
    //      high-feedback comb or sustained insert state keeps ringing into the
    //      channel buf for many seconds after retirement, audible as an
    //      "effect overlay" on the new active rhythm — the symptom from the
    //      first listening test on Step 3. The sample (SamplePlayer voices)
    //      and the amp envelope release are preserved — those are the
    //      legitimate "natural tail" Stage 34 is supposed to give.
    //   3. retired flag — switches isFullyDrained() to a voices-only drain
    //      criterion for ampRelToEnd engines (which don't get noteOff'd so
    //      amp env never reaches idle).
    void markRetired() noexcept;

private:
    juce::AudioFormatManager formatManager;

    juce::ReadWriteLock      bufferLock;
    juce::AudioBuffer<float> buffer;
    double originalSampleRate = 44100.0;
    double playbackRatio      = 1.0;

    std::array<SamplePlayer, MaxVoices> voices;
    int    nextVoice         = 0;
    double currentSampleRate = 44100.0;
    bool   sampleLoaded      = false;

    VoiceParams params;        // base params — updated from message thread via setParams()
    VoiceParams activeParams;  // audio thread only — what process() actually uses
                               // (= params unless modulation overrides it each block)

    // Single engine-level amp envelope (#627). Pre-Stage-35 there was one ADSR per
    // voice slot — that model never composed cleanly with the SHARED filter + insert
    // chain that follows it (filter resonance / comb feedback could outlast any single
    // voice's env), so #416 had to forcibly reset filter+insert state in markRetired()
    // to kill the residual ringing. With the new "filter-then-amp" signal flow, voices
    // mix raw into tempBuffer → filter + insert process the combined signal → engine
    // ampEnv multiplies the result at the END of the chain. Filter / insert tails ring
    // naturally and drain through the env release. Trade-off: rapid retriggers reset the
    // engine env (no per-voice envelope independence on overlapping voices) — fine for
    // drum-style triggers and pad use is mitigated by pattern legato (#419).
    juce::ADSR  ampEnv;
    juce::ADSR  filterEnv;
    juce::ADSR  pitchEnv;
    MultiModeFilter voiceFilter;             // drive + main filter + lo-cut, all owned here
    juce::AudioBuffer<float> tempBuffer;     // voice sum → filter → insert → env-gate → output

public:
    InsertProcessor insertProc;  // exposed so VoiceSection can read grReduction for the GR meter
private:

    // per-sample pitch ratio buffer feeding SamplePlayer. Filled each block
    // from the pitch envelope (sample-accurate) + base semitones + smoothed pitchMod.
    // Reserved to blockSize in prepareToPlay so no audio-thread alloc.
    std::vector<double> pitchRatioBuffer;
    // 5 ms linear ramp on pitchMod between block targets. Read per-sample
    // inside the pitchRatioBuffer fill loop so fast mod doesn't zipper.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPitchMod;

    float             accentGain = 1.0f;

    VoiceParams       pendingParams;
    juce::SpinLock    pendingLock;
    std::atomic<bool> paramsDirty { false };

    // Stage 34 Step 3 fix: set true by markRetired(). Used by isFullyDrained()
    // to switch to a voices-only drain criterion when ampRelToEnd is true
    // (those engines don't get noteOff'd so amp env never reaches idle).
    bool retired = false;

    // Post-retirement drain timer. With the insert AFTER ampEnv in the signal
    // chain (T12), feedback-based inserts (Karplus, etc.) can ring on past env
    // idle. isFullyDrained holds the engine alive for this many process() calls
    // beyond env-idle so the insert's internal feedback decays audibly instead
    // of being cut by engine destruction during hot-swap.
    int retireDrainBlocks = 0;

    void applyPendingParams();   // audio thread only — syncs activeParams from pendingParams
    void syncEnvelopes();        // audio thread only — pushes activeParams into ADSR objects
    void syncFilter();           // audio thread only — pushes activeParams into filter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEngine)
};
