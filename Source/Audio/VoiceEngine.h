#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "SamplePlayer.h"
#include "VoiceParams.h"
#include "AudioFilters.h"
#include "InsertProcessor.h"

#include <array>
#include <atomic>

// Per-rhythm voice chain: SamplePlayer pool → Amp ADSR → Filter + Filter ADSR → level trim.
// Pitch ratio is derived from VoiceParams (octave/semitones/fine) plus a pitch envelope.
class VoiceEngine
{
public:
    static constexpr int MaxVoices = 4;

    VoiceEngine();

    void prepareToPlay(double sampleRate, int blockSize);
    void loadFile(const juce::File& file);
    void trigger(bool isAccented = false);
    void process(juce::AudioBuffer<float>& output, int numSamples);
    bool hasSample() const;

    // Called from message thread. Stores params; audio thread picks them up in process().
    void setParams(const VoiceParams& p);

    // Called from audio thread only (PluginProcessor::processBlock).
    // Applies a fully-computed modulated VoiceParams for this block, bypassing the
    // message-thread pending mechanism.  Must be called before process().
    void setActiveParams(const VoiceParams& p);

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
    juce::ADSR  ampEnv;
    juce::ADSR  filterEnv;
    juce::ADSR  pitchEnv;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::AudioBuffer<float> tempBuffer;
    juce::AudioBuffer<float> notchBuffer;  // pre-filter dry copy for notch = dry - BP
    InsertProcessor insertProc;

    float             accentGain = 1.0f;

    VoiceParams       pendingParams;
    juce::SpinLock    pendingLock;
    std::atomic<bool> paramsDirty { false };

    void applyPendingParams();   // audio thread only — syncs activeParams from pendingParams
    void syncEnvelopes();        // audio thread only — pushes activeParams into ADSR objects
    void syncFilter();           // audio thread only — pushes activeParams into filter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceEngine)
};
