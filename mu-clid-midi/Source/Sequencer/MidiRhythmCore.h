#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/MidiOutputEngine.h"
#include "Sequencer/SequencerEngine.h"    // mu-Clid's, shared until Rhythm / SequencerEngine move to mu-core
#include "Sequencer/RhythmPlayState.h"    // mu-Clid's, shared with RhythmCircle

#include <atomic>

namespace mu_clid_midi {

using mu_clid::Rhythm;
using mu_clid::RhythmPlayState;
using mu_clid::SequencerEngine;

// Everything mu-Clid MIDI does between the host block and its MIDI output, with no AudioProcessor
// around it: the one rhythm, the note engine, the parameter sync and the lock that keeps the message
// thread edits and the audio thread apart. PluginProcessor delegates to it, and the tests drive it
// from a bare processor, so the audio path they check is the shipped one.
class MidiRhythmCore : private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit MidiRhythmCore(juce::AudioProcessorValueTreeState& apvtsToUse);
    ~MidiRhythmCore() override;

    void prepare(double sampleRate, int samplesPerBlock);

    // Audio thread. Steps the sequencer and emits this block's MIDI; when the message thread holds
    // the rhythm lock the block is skipped, but a pending note-off is still flushed.
    void processBlock(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples);

    // Audio thread. Emits the note-off of the last note once its 20 ms are up, without triggering
    // anything: for a bypassed or skipped block, which must not leave a note hanging.
    void flushPendingNoteOff(juce::MidiBuffer& midi, int numSamples);

    // Message thread. Restores a host session under the rhythm lock and rebuilds the rhythm from the
    // parameters; the caller suspends processing around it. False with `error` set if unreadable.
    bool restoreSession(const juce::String& xml, juce::String& error);

    // Message thread. Rebuilds the pattern if an off-thread parameter change asked for it.
    void servicePendingRebuild();

    bool isApvtsLoading() const noexcept { return apvtsLoading.load(std::memory_order_acquire); }

    // The lock the message thread holds around rhythm edits; exposed so a test can hold it from a
    // second thread and see the audio thread skip a block.
    juce::CriticalSection& getRhythmLock() noexcept { return rhythmsLock; }

    SequencerEngine sequencer;

    // UI play state: written by the audio thread, read by the editor at 30 Hz.
    RhythmPlayState     rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f };   // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };    // most recent beat position (for the UI playhead)

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(const char* suffix, float value);
    void syncRhythmFromApvts();
    void pushRhythmToApvts();
    void advanceSequencer(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples);

    juce::AudioProcessorValueTreeState& apvts;
    MidiOutputEngine midiEngine;

    // suspendProcessing() does not wait for a running block, so the message thread takes this
    // around any edit to the rhythm and the audio thread try-locks it.
    juce::CriticalSection rhythmsLock;

    // Raised for the whole of a bulk push or restore so parameterChanged skips the pattern rebuild.
    std::atomic<bool> apvtsLoading { false };

    // Set when a parameter changed off the message thread (VST3 automation runs on the audio
    // thread), where a pattern rebuild would allocate; the message thread rebuilds it.
    std::atomic<bool> patternRebuildPending { false };

    // Cached parameter pointers, so processBlock pays no per-block id lookup.
    std::atomic<float>* midiNotePtr  = nullptr;
    std::atomic<float>* accentAmtPtr = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRhythmCore)
};

} // namespace mu_clid_midi
