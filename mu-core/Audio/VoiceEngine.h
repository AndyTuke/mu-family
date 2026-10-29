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
    bool loadFile(const juce::File& file);   // false when the file cannot be decoded; the old sample keeps playing
    void clearSample();

    // A sample read from a file and kept, so a staged preset can do its file work early and install at its boundary
    // (loadFile is decodeFile then installSample). Message thread; a default one holds nothing.
    struct DecodedSample
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        bool isValid() const noexcept { return buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0 && sampleRate > 0.0; }
    };
    DecodedSample decodeFile(const juce::File& file);     // empty when the file cannot be read; the engine is untouched
    void          installSample(DecodedSample&& sample);  // brief: only the buffer swap is under the lock; an empty one changes nothing
    DecodedSample takeSample();                           // moves this engine's sample out, leaving it empty
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

    // The base params the audio thread last applied (audio thread, or a test with no audio running).
    const VoiceParams& getAppliedParams() const noexcept { return params; }

#if defined(MU_VOICE_ENGINE_TEST_SEAM) && MU_VOICE_ENGINE_TEST_SEAM
    // Test seam, compiled only into targets that define MU_VOICE_ENGINE_TEST_SEAM (mu-core-tests):
    // called by setParams while it holds the pending-params lock, so a test can hold the lock open and
    // check the audio thread neither blocks nor loses the update. Absent from every shipping target.
    static inline std::atomic<void (*)()> setParamsHoldHook { nullptr };
    // Test seam: while true, process() takes its contended-block path, as when the sample-buffer try-lock misses.
    static inline std::atomic<bool> forceBufferContention { false };
#endif

    // Called from audio thread only (PluginProcessor::processBlock).
    // Applies a fully-computed modulated VoiceParams for this block, bypassing the
    // message-thread pending mechanism.  Must be called before process().
    void setActiveParams(const VoiceParams& p);

    // True when no sample voice is mid-playback (or pending trigger) AND the amp envelope is idle (a retired
    // engine: and either its output has stayed below -100 dBFS for 50 ms, or its 2 s post-idle drain budget is
    // spent; a retired ampRelToEnd engine: once its voices end). The budget bounds a feedback insert that still
    // rings, which the retire bin closes with a short declick. Pure inspection, no side effects; the audio thread
    // asks it after each retired block.
    bool isFullyDrained() const noexcept;

    // Prepares the engine for its tail as a retired engine: the envelopes enter their release (the amp envelope
    // only when ampRelToEnd is off, so a release-to-end sample plays through at its level), the post-idle drain
    // budget (2 s of prepared-size blocks) starts, and the retired flag switches isFullyDrained to its retired
    // criteria. Filter and insert state are kept (their tails ring down under the amp envelope). Called once by
    // whoever then owns the engine alone: mu-Clid's EngineTail at its first tail block, on the audio thread.
    // Allocates nothing.
    void markRetired() noexcept;

    // Lets the held note of a LIVE engine go: the envelopes enter release (the amp env otherwise sits at
    // sustain until the next trigger) and the sample voices finish on their own. Unlike markRetired it
    // does not freeze the engine or reset the filter / insert state. The caller must exclude the audio
    // thread (hold the product's layer lock, which the audio block try-locks); allocates nothing.
    // With ampRelToEnd the sample is meant to play through at its level, so the amp envelope is left.
    void releaseHeldNote() noexcept;

private:
    juce::AudioFormatManager formatManager;

    // Guards buffer / originalSampleRate / playbackRatio (and sampleLoaded's writes) between the message thread's swaps
    // (prepareToPlay, loadFile, clearSample: brief, no I/O inside) and process(), which only try-locks it.
    // A spin lock, not juce::ReadWriteLock, whose reader bookkeeping can allocate on the audio thread.
    juce::SpinLock           bufferLock;
    juce::AudioBuffer<float> buffer;
    double originalSampleRate = 44100.0;
    double playbackRatio      = 1.0;

    std::array<SamplePlayer, MaxVoices> voices;
    int    nextVoice         = 0;
    double currentSampleRate = 44100.0;
    // Written under bufferLock; atomic because hasSample() reads it without the lock.
    std::atomic<bool> sampleLoaded { false };

    VoiceParams params;        // base params — updated from message thread via setParams()
    VoiceParams activeParams;  // audio thread only — what process() actually uses
                               // (= params unless modulation overrides it each block)

    // Single engine-level amp envelope. Pre-Stage-35 there was one ADSR per
    // voice slot — that model never composed cleanly with the SHARED filter + insert
    // chain that follows it (filter resonance / comb feedback could outlast any single
    // voice's env), so the old design had to forcibly reset filter+insert state in
    // markRetired() to kill the residual ringing. With the new "filter-then-amp" signal
    // flow, voices mix raw into tempBuffer → filter + insert process the combined signal
    // → engine ampEnv multiplies the result at the END of the chain. Filter / insert
    // tails ring naturally and drain through the env release. Trade-off: rapid retriggers
    // reset the engine env (no per-voice envelope independence on overlapping voices) —
    // fine for drum-style triggers and pad use is mitigated by pattern legato.
    juce::ADSR  ampEnv;
    juce::ADSR  filterEnv;
    juce::ADSR  pitchEnv;
    // Set at each envelope's noteOff, cleared at its noteOn: syncEnvelopes leaves the parameters alone while
    // one is set and the envelope is still running (mu_env::applyParameters).
    bool ampReleasing = false, filterReleasing = false, pitchReleasing = false;
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
    // A retired engine's run of rendered output below kRetireSilentLevel, in samples (audio thread; reset by any
    // louder block, left alone by a contended one), and the run that counts as drained (50 ms, set by markRetired).
    // The hold must outlast the longest silent gap a ringing insert can leave: the Karplus loop's period is at most
    // about 31 ms (its lowest note, C1); a lower Karplus range or a delay-type insert needs a longer hold.
    static constexpr float kRetireSilentLevel = 1.0e-5f;   // -100 dBFS
    int retireSilentSamples = 0;
    int retireSilentHold    = 0;

    void applyPendingParams();   // audio thread only — syncs activeParams from pendingParams
    void syncEnvelopes();        // audio thread only — pushes activeParams into ADSR objects
    void syncFilter();           // audio thread only — pushes activeParams into filter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEngine)
};
