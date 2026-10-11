#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/MidiOutputEngine.h"
#include "Sequencer/SequencerEngine.h"    // mu-Clid's, shared until Rhythm / SequencerEngine move to mu-core
#include "Persistence/RhythmParamTable.h"   // mu-Clid: kRhythmParamCount for the pending slots
#include "Sequencer/RhythmPlayState.h"    // mu-Clid's, shared with RhythmCircle

#include <array>
#include <atomic>
#include <thread>

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

    // Message thread (the 30 Hz timer). Applies the rhythm parameter changes that arrived off the
    // message thread, then rebuilds the pattern if any of them needs it.
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
    void drainPendingParams();
    bool isMessageThread() const;
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

    // Rhythm parameter changes from off the message thread (VST3 automation runs on the audio
    // thread), one slot per suffix of kRhythmParamDefs. A producer stores the value, then raises the
    // flag; the message thread clears the flag, then reads the value. Several producers are safe, and a
    // burst of changes to one suffix coalesces to the latest value, so the queue cannot overflow and
    // never allocates or waits. A change that races the drain is at worst applied twice with the same
    // value. Only the message thread writes the Rhythm fields (under rhythmsLock, then voiceParamsLock,
    // then the sequencer's pattern lock), so the audio thread never writes them.
    struct PendingParam
    {
        std::atomic<float> value { 0.0f };
        std::atomic<bool>  dirty { false };
    };
    std::array<PendingParam, mu_clid::kRhythmParamCount> pendingParams;

    // Set when a drain wanted a pattern rebuild but a bulk load was running; the next service does it.
    std::atomic<bool> patternRebuildPending { false };

    // Where a MessageManager does not exist (a headless test), the thread that built the core.
    std::thread::id ownerThread;

    // Cached parameter pointers, so processBlock pays no per-block id lookup.
    std::atomic<float>* midiNotePtr  = nullptr;
    std::atomic<float>* accentAmtPtr = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRhythmCore)
};

} // namespace mu_clid_midi
