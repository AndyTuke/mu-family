#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SamplePlayer.h"
#include "VoiceParams.h"
#include "MultiModeFilter.h"
#include "InsertProcessor.h"
#include "Filters/Hp24Filter.h"
#include "../MuLimits.h"

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

    // Per-voice amp envelopes — one ADSR per sample slot so long releases play
    // out independently when a retrigger claims a different slot. All envelopes
    // share the same A/D/S/R parameters (driven by activeParams.ampEnv*); only
    // their state diverges. Filter / pitch envelopes remain monophonic (one
    // shared envelope, gates filter cutoff modulation and per-sample pitch
    // ratio respectively) — those changes are tracked separately.
    std::array<juce::ADSR, MaxVoices> ampEnvs;
    juce::ADSR  filterEnv;
    juce::ADSR  pitchEnv;
    MultiModeFilter voiceFilter;             // owns SVF / Ladder / 1-pole / biquad / comb state
    Hp24Filter      lowCutFilter;            // 4-pole HPF inline with voiceFilter — bypassed when cutoff <= 0
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLowCutHz;
    juce::AudioBuffer<float> tempBuffer;     // post-envelope sum that feeds filter/insert

    // Per-voice scratch buffer — each SamplePlayer writes into its own
    // voiceBuffers[i] additively, ampEnvs[i] applied in place, then summed
    // into tempBuffer. Sized in prepareToPlay so no audio-thread alloc.
    std::array<juce::AudioBuffer<float>, MaxVoices> voiceBuffers;

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

    void applyPendingParams();   // audio thread only — syncs activeParams from pendingParams
    void syncEnvelopes();        // audio thread only — pushes activeParams into ADSR objects
    void syncFilter();           // audio thread only — pushes activeParams into filter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEngine)
};
