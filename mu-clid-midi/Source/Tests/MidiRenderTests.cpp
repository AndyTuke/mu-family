// Render-style tests of the shipped audio path: a bare processor hosts MidiRhythmCore (the code
// PluginProcessor delegates to) and is driven block by block with a simulated host transport. The
// checks are about note hygiene: every note-on gets its note-off, none stays active, whatever the
// block size, sample rate or interruption.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tests/StubProcessor.h"
#include "Sequencer/MidiRhythmCore.h"
#include "Persistence/SessionState.h"

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

using namespace mu_clid_midi;

//==============================================================================
// Allocation counter: counts operator new calls made while the calling thread has counting switched on.
namespace alloc_count {
inline thread_local bool counting = false;
inline thread_local int  count    = 0;
}

void* operator new(std::size_t size)
{
    if (alloc_count::counting) ++alloc_count::count;
    if (void* p = std::malloc(size > 0 ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

// The bare processor with the shipped core attached, and a host transport the test controls.
struct RenderStub : tests::StubProcessor
{
    MidiRhythmCore core { apvts };

    double       sampleRate = 44100.0;
    double       bpm        = 120.0;
    bool         playing    = false;
    juce::int64  hostSample = 0;      // the host timeline: advances only while the transport plays

    void prepareToPlay(double sr, int block) override { sampleRate = sr; core.prepare(sr, block); }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        core.processBlock(playing, beatAt(hostSample), midi, n);
        if (playing) hostSample += n;
    }

    using juce::AudioProcessor::processBlockBypassed;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        core.flushPendingNoteOff(midi, buffer.getNumSamples());
        if (playing) hostSample += buffer.getNumSamples();   // the host keeps rolling while bypassed
    }

    void setStateInformation(const void* data, int size) override
    {
        juce::String error;
        core.restoreSession(juce::String::fromUTF8((const char*) data, size), error);
    }

    double beatAt(juce::int64 sample) const { return (double) sample / sampleRate * bpm / 60.0; }
};

struct NoteEvent { bool on; int note; juce::int64 time; };

enum class Kind { normal, bypassed };

// Drives a RenderStub in fixed-size blocks and records the MIDI it emits at absolute sample times.
struct Renderer
{
    Renderer(RenderStub& stub, double sr, int blockSize)
        : p(stub), block(blockSize), buffer(0, blockSize)
    {
        p.prepareToPlay(sr, blockSize);
    }

    // Renders one block; returns true when it emitted a note-on.
    bool step(Kind kind = Kind::normal)
    {
        juce::MidiBuffer midi;
        if (kind == Kind::normal) p.processBlock(buffer, midi);
        else                      p.processBlockBypassed(buffer, midi);

        bool sawOn = false;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())       { events.push_back({ true,  m.getNoteNumber(), clock + meta.samplePosition }); sawOn = true; }
            else if (m.isNoteOff()) { events.push_back({ false, m.getNoteNumber(), clock + meta.samplePosition }); }
        }
        clock += block;
        return sawOn;
    }

    void run(double seconds, Kind kind = Kind::normal)
    {
        const juce::int64 end = clock + (juce::int64) (seconds * p.sampleRate);
        while (clock < end) step(kind);
    }

    // Renders until a block emits a note-on; false if none within maxSeconds.
    bool runUntilNoteOn(double maxSeconds)
    {
        const juce::int64 end = clock + (juce::int64) (maxSeconds * p.sampleRate);
        while (clock < end)
            if (step()) return true;
        return false;
    }

    int count(bool on) const
    {
        int n = 0;
        for (const auto& e : events) if (e.on == on) ++n;
        return n;
    }

    // Empty when every note-on is followed by its own note-off before the next one and nothing is
    // left sounding; otherwise what went wrong.
    juce::String pairProblem() const
    {
        int active = -1;
        for (const auto& e : events)
        {
            if (e.on)
            {
                if (active >= 0) return "note-on " + juce::String(e.note) + " while " + juce::String(active) + " still sounding";
                active = e.note;
            }
            else
            {
                if (active != e.note) return "note-off " + juce::String(e.note) + " does not match active " + juce::String(active);
                active = -1;
            }
        }
        return active >= 0 ? "note " + juce::String(active) + " left active" : juce::String();
    }

    // Samples from the first unmatched-on's note-off back to its on: the longest note in the run.
    juce::int64 longestNote() const
    {
        juce::int64 longest = 0, onTime = 0;
        for (const auto& e : events)
        {
            if (e.on) onTime = e.time;
            else      longest = juce::jmax(longest, e.time - onTime);
        }
        return longest;
    }

    RenderStub& p;
    int block;
    juce::AudioBuffer<float> buffer;
    juce::int64 clock = 0;
    std::vector<NoteEvent> events;
};

} // namespace

//==============================================================================
class MidiRenderTest : public juce::UnitTest
{
public:
    MidiRenderTest() : juce::UnitTest("mu-Clid MIDI render", "MidiProduct") {}

    // 20 ms note length, plus one block of timing slack (the engine counts whole blocks).
    static juce::int64 noteLimit(double sr, int block) { return (juce::int64) (sr * 0.020) + block + 1; }

    void runTest() override
    {
        const juce::ScopedJuceInitialiser_GUI initialiser;   // a message thread, as in a host

        for (const double sr : { 44100.0, 192000.0 })
            for (const int block : { 1, 64, 512, 2048 })
            {
                const juce::String where = juce::String(sr, 0) + " Hz, block " + juce::String(block);

                beginTest("Plain playback keeps note pairs matched (" + where + ")");
                {
                    RenderStub p; Renderer r(p, sr, block);
                    p.playing = true;
                    r.run(2.2);
                    p.playing = false;
                    r.run(0.1);
                    expect(r.count(true) >= 3, "notes were played");
                    expectEquals(r.count(true), r.count(false));
                    expectEquals(r.pairProblem(), juce::String());
                    expect(r.longestNote() <= noteLimit(sr, block), "a note ran past 20 ms");
                }

                beginTest("Transport stop mid-note sends the note-off (" + where + ")");
                {
                    RenderStub p; Renderer r(p, sr, block);
                    p.playing = true;
                    expect(r.runUntilNoteOn(1.0), "a note started");
                    p.playing = false;
                    r.run(0.1);
                    expectEquals(r.count(true), r.count(false));
                    expectEquals(r.pairProblem(), juce::String());
                    expect(r.longestNote() <= noteLimit(sr, block), "the note-off came late");
                }

                beginTest("A MIDI note change mid-note ends the old note and plays the new (" + where + ")");
                {
                    RenderStub p; Renderer r(p, sr, block);
                    p.playing = true;
                    expect(r.runUntilNoteOn(1.0), "a note started");
                    p.setActual("lite_midiNote", 60.0f);
                    r.run(1.2);
                    expectEquals(r.events.front().note, 36);
                    bool newNote = false;
                    for (const auto& e : r.events) if (e.on && e.note == 60) newNote = true;
                    expect(newNote, "the new note played");
                    expectEquals(r.pairProblem(), juce::String());
                }

                beginTest("A bypassed block lets the note-off out (" + where + ")");
                {
                    RenderStub p; Renderer r(p, sr, block);
                    p.playing = true;
                    expect(r.runUntilNoteOn(1.0), "a note started");
                    r.run(0.1, Kind::bypassed);   // only bypassed blocks from here: the flush must come from them
                    expectEquals(r.count(true), r.count(false));
                    expectEquals(r.pairProblem(), juce::String());
                    expect(r.longestNote() <= noteLimit(sr, block), "the note-off came late");
                    r.run(0.7);                    // and the effect plays on afterwards
                    expect(r.count(true) >= 2, "playback resumed");
                    expectEquals(r.pairProblem(), juce::String());
                }

                beginTest("A block skipped on lock contention lets the note-off out (" + where + ")");
                {
                    RenderStub p; Renderer r(p, sr, block);
                    p.playing = true;
                    expect(r.runUntilNoteOn(1.0), "a note started");

                    // Another thread holds the rhythm lock, as the message thread does during an edit.
                    std::atomic<bool> held { false }, release { false };
                    std::thread holder([&]
                    {
                        const juce::ScopedLock sl(p.core.getRhythmLock());
                        held = true;
                        while (! release) std::this_thread::yield();
                    });
                    while (! held) std::this_thread::yield();
                    r.run(0.1);
                    release = true;
                    holder.join();

                    expectEquals(r.count(true), r.count(false));
                    expectEquals(r.pairProblem(), juce::String());
                    expect(r.longestNote() <= noteLimit(sr, block), "the note-off came late");
                    r.run(0.7);
                    expect(r.count(true) >= 2, "playback resumed");
                    expectEquals(r.pairProblem(), juce::String());
                }

                beginTest("Restoring state mid-play leaves no note hanging (" + where + ")");
                {
                    RenderStub p, source; Renderer r(p, sr, block);
                    source.setActual("r0_stepsA", 12.0f);
                    source.setActual("r0_hitsA", 12.0f);
                    source.setActual("lite_midiNote", 40.0f);
                    const auto state = session::toXmlString(source.apvts).toStdString();

                    p.playing = true;
                    expect(r.runUntilNoteOn(1.0), "a note started");
                    p.setStateInformation(state.data(), (int) state.size());
                    r.run(1.2);

                    expectEquals(p.core.sequencer.getRhythm(0).genA.steps, 12);
                    bool newNote = false;
                    for (const auto& e : r.events) if (e.on && e.note == 40) newNote = true;
                    expect(newNote, "the restored note played");
                    expectEquals(r.pairProblem(), juce::String());
                }
            }

        beginTest("A restore applies an old state's rhythm name and colour");
        {
            RenderStub p;
            juce::ValueTree legacy("MuClidPreset");
            legacy.setProperty("r0_name", "Kick", nullptr);
            legacy.setProperty("r0_colour", 3, nullptr);
            const auto xml = legacy.toXmlString().toStdString();
            p.setStateInformation(xml.data(), (int) xml.size());
            expectEquals(juce::String(p.core.sequencer.getRhythm(0).name), juce::String("Kick"));
            expectEquals(p.core.sequencer.getRhythm(0).colourIndex, 3);
        }

        beginTest("A parameter change off the message thread allocates nothing and defers the pattern rebuild");
        {
            RenderStub with;
            Renderer r(with, 44100.0, 512);
            with.playing = true;
            r.run(0.3);
            const int stepsBefore = with.core.sequencer.getPatternLength(0);

            // Baseline: the cost of the host's own notification path with nothing of ours attached.
            // StubProcessor has no core, so its parameter set is the same minus our listener.
            tests::StubProcessor bare;
            int bareAllocs = 0, coreAllocs = 0;
            std::thread audio([&]
            {
                alloc_count::count = 0; alloc_count::counting = true;
                bare.setActual("r0_stepsA", 9.0f);
                alloc_count::counting = false; bareAllocs = alloc_count::count;

                alloc_count::count = 0; alloc_count::counting = true;
                with.setActual("r0_stepsA", 9.0f);
                alloc_count::counting = false; coreAllocs = alloc_count::count;

                with.setActual("mstrLoop", 2.0f);
            });
            audio.join();

            expectEquals(with.core.sequencer.getMasterLoopSteps(), 32);
            expectEquals(with.core.sequencer.getRhythm(0).genA.steps, 9);   // the field is written at once
            r.step();
            expectEquals(with.core.sequencer.getPatternLength(0), stepsBefore);   // but the pattern waits

            with.core.servicePendingRebuild();   // the message thread's timer
            r.step();
            expect(with.core.sequencer.getPatternLength(0) != stepsBefore, "the pattern was rebuilt");

            // The setter's own notification work is the same with or without our listener; ours adds none.
            expect(coreAllocs <= bareAllocs, "our listener allocated: " + juce::String(coreAllocs) + " vs " + juce::String(bareAllocs));
        }
    }
};

static MidiRenderTest midiRenderTest;
