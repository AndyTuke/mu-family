#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/MidiNoteVoice.h"
#include "Sequencer/ArpRunner.h"        // mu-toni: the arp clock and note scan (BasicArpRunner)
#include "Sequencer/HeldNotes.h"        // mu-toni: the keys the arp hears
#include "Sequencer/Layer.h"            // mu-core: the modulators (control sequences + matrix) live on a Layer
#include "Modulation/MuToniMidiModDest.h"
#include "Modulation/LastResolved.h"    // mu-core: hold the last values when the modulator lock is busy

#include <array>
#include <string_view>
#include <unordered_map>

#include <atomic>

// The audio path of mu-Toni MIDI, kept apart from the AudioProcessor so the render tests cover it. It is mu-Toni's arpeggiator
// (the same BasicArpRunner, note pool and scan) driving a MidiNoteVoice instead of a synth voice:
//   held keys in -> arp -> MIDI notes out, on the host transport.
// Note messages in the input are consumed (the held keys feed the arp); every other message passes through.
namespace mu_toni_midi
{

// Parameter ids. The arp rows are mu-Toni's own a0_* ids (slot 0), so a layer preset can be shared later.
inline constexpr const char* kMidiInChId  = "midiInCh";   // 0 = Omni, 1..16 (the id mu-Toni uses)
inline constexpr const char* kMidiOutChId = "midiOutCh";  // 1..16
inline constexpr const char* kMidiVelId   = "midiVel";    // 1..127: the velocity of an unaccented note

// The arp's modulators: the control sequences and the matrix every mu layer has, on a layer of their own so the shared modulator
// panel and the saved-state code work on it unchanged. It makes no sound; only the arp rows can be targets (MuToniMidiModDest.h).
struct MidiArpLayer : Layer
{
    bool isValidDest(const std::string& id) const override { return isValidModDest(id); }
};

class MidiArpCore
{
public:
    explicit MidiArpCore(juce::AudioProcessorValueTreeState& apvts);

    // Message thread, not processing: sizes the scratch buffers for the host's block size.
    void prepare(double sampleRate, int maxBlock);

    // Back to the silent start state (a transport stop, a state restore). Does not write a note-off: use flushNoteOff for that.
    void reset() noexcept;

    // Audio thread. `midi` holds the block's input and is replaced by the block's output.
    void processBlock(bool playing, double startBeat, double bpm, juce::MidiBuffer& midi, int numSamples);

    // Audio thread: releases a sounding note into `midi` at sample 0 (a bypassed block, so no note hangs).
    void flushNoteOff(juce::MidiBuffer& midi);

    // The modulators, for the editor's modulator panel and the saved state (message thread).
    MidiArpLayer&       getLayer() noexcept       { return layer; }
    const MidiArpLayer& getLayer() const noexcept { return layer; }
    // The host beat of the last block, for the modulator playhead (any thread).
    double lastBeat() const noexcept { return uiBeat.load(std::memory_order_relaxed); }

    // UI (any thread): the last step the arp fired, and the note now sounding (-1 none).
    int  lastStep() const noexcept       { return uiLastStep.load(std::memory_order_relaxed); }
    int  soundingNote() const noexcept   { return uiNote.load(std::memory_order_relaxed); }
    bool playing() const noexcept        { return uiPlaying.load(std::memory_order_relaxed); }

private:
    float p(const std::atomic<float>* a, float fallback = 0.0f) const noexcept { return a != nullptr ? a->load(std::memory_order_relaxed) : fallback; }

    MidiArpLayer layer;   // first: the arrays below point into the parameters, and the modulators sit on this
    mu_toni::BasicArpRunner<MidiNoteVoice> runner;
    mu_toni::HeldNotes heldNotes;
    juce::MidiBuffer   outBuf;
    juce::AudioBuffer<float> dummy;   // the runner "renders" into this; nothing reads it
    double sampleRate = 44100.0;
    int    maxBlock = 512;
    int    lastInCh = -1;

    // Cached parameter pointers (never looked up by id on the audio thread).
    const std::atomic<float>*scale{}, *root{}, *roct{}, *chord{}, *inv{}, *octs{}, *dir{}, *rate{}, *gate{}, *leg{}, *snap{}, *trig{},
                             *acc{}, *accLen{}, *accPat{}, *stepMask{}, *inCh{}, *outCh{}, *vel{};

    // Modulation resolve inputs: parallel arrays for mu_mod::resolveLane (one per target, in kModDestTable order) and its work map
    // (pre-sized, so the audio thread only overwrites).
    std::array<const std::atomic<float>*, kNumModDests>       modDestAtoms {};
    std::array<const char*, kNumModDests>                     modDestIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests>  modDestRanges {};
    std::unordered_map<std::string_view, float>               modParamValues;
    mu_mod::LastResolved<kNumModDests>                        lastResolved;

    std::atomic<int>  uiLastStep { -1 }, uiNote { -1 };
    std::atomic<double> uiBeat { 0.0 };
    std::atomic<bool> uiPlaying { false };
};

} // namespace mu_toni_midi
