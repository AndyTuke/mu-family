#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/MidiOutputEngine.h"
#include "Sequencer/RhythmSequencer.h"
#include "Persistence/RhythmParamTable.h"   // kRhythmParamCount for the pending slots
#include "Sequencer/RhythmPlayState.h"    // shared with RhythmCircle (mu-core)

#include <array>
#include <atomic>
#include <thread>

namespace mu_clid_midi {

using mu_core::Rhythm;
using mu_core::RhythmPlayState;
using mu_core::RhythmSequencer;

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

    // Audio thread. Steps the sequencer and emits this block's MIDI. Takes no lock: it reads only the
    // sequencer's own pattern snapshot and atomics. A block that starts during a session restore is
    // skipped, but a pending note-off is still flushed.
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

    // Blocks the audio thread skipped (only a restore does that); a test checks that parameter
    // drains never add to it.
    int getSkippedBlockCount() const noexcept { return skippedBlocks.load(std::memory_order_relaxed); }

    // The lock that serialises the message-thread writers of the rhythm (drain, restore). The audio
    // thread never takes it; exposed so a test can hold it from a second thread and see no block skip.
    juce::CriticalSection& getRhythmLock() noexcept { return rhythmsLock; }

    // The one rhythm, owned here and bound to the sequencer's slot 0 (the sequencer does not own rhythms).
    Rhythm          rhythm;
    RhythmSequencer sequencer;

    // UI play state: written by the audio thread, read by the editor at 30 Hz.
    RhythmPlayState     rhythmPlayState;
    std::atomic<float>  beatFraction     { 0.0f };   // fractional position within the current 1/16 step
    std::atomic<bool>   sequencerPlaying { false };
    std::atomic<double> lastBeatPos      { 0.0 };    // most recent beat position (for the UI playhead)

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncRhythmParam(const char* suffix);
    void drainPendingParams();
    void publishStepCounts();
    int  findParamIndex(const char* suffix) const noexcept;
    bool isMessageThread() const;
    void syncRhythmFromApvts();
    void pushRhythmToApvts();
    void advanceSequencer(bool playing, double beatPos, juce::MidiBuffer& midi, int numSamples);

    juce::AudioProcessorValueTreeState& apvts;
    MidiOutputEngine midiEngine;

    // Serialises the message-thread writers of the Rhythm (the drain, a restore). The audio thread
    // never takes it: it reads no Rhythm field (the pattern comes through the sequencer's own
    // snapshot under its pattern lock, the step counts for the UI are published from here), so there
    // is nothing for it to protect on that side. Nothing here takes Rhythm::voiceParamsLock for the
    // same reason; it guards voice-engine fields this product does not have. Lock order: this, then
    // the sequencer's pattern lock.
    juce::CriticalSection rhythmsLock;

    // Raised for the whole of a restore so the audio thread skips blocks while the parameters, the
    // loop length and the pattern are replaced as a set instead of seeing them half applied.
    std::atomic<bool> restoreInProgress { false };
    std::atomic<int>  skippedBlocks     { 0 };

    // Raised for the whole of a bulk push or restore so parameterChanged skips the pattern rebuild.
    std::atomic<bool> apvtsLoading { false };

    // Rhythm parameter changes from off the message thread (VST3 automation runs on the audio
    // thread), one dirty flag per suffix of kRhythmParamDefs. The value is not queued: the drain reads
    // the parameter's own atomic, so it can never apply something older than the parameter. A burst of
    // changes to one suffix coalesces into one apply, and the producer never allocates or waits. Only
    // the message thread writes the Rhythm fields (under rhythmsLock, then the sequencer's pattern
    // lock), so the audio thread never writes them.
    std::array<std::atomic<bool>, mu_core::kRhythmParamCount> pendingDirty {};

    // Per kRhythmParamDefs entry, the parameter's raw value (null if this product has no such
    // parameter); built once in the constructor so no lookup by id happens afterwards.
    std::array<std::atomic<float>*, mu_core::kRhythmParamCount> rawParams {};

    // Suffix to index in kRhythmParamDefs, sorted by suffix, for the audio thread's lookup by binary
    // search instead of a scan. Built in the constructor and never changed.
    struct SuffixIndex { const char* suffix; int index; };
    std::array<SuffixIndex, mu_core::kRhythmParamCount> suffixIndex {};
    int suffixIndexCount = 0;

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
