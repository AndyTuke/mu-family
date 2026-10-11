// Render-style tests of the shipped audio path: a bare processor hosts MidiRhythmCore (the code
// PluginProcessor delegates to) and is driven block by block with a simulated host transport. The
// checks are about note hygiene: every note-on gets its note-off, none stays active, whatever the
// block size, sample rate or interruption.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tests/StubProcessor.h"
#include "Sequencer/MidiRhythmCore.h"
#include "Persistence/SessionState.h"
#include "Sequencer/AccentVelocity.h"
#include "Sequencer/HitGenerator.h"

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

struct NoteEvent { bool on; int note; juce::int64 time; int velocity; };

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
            if (m.isNoteOn())       { events.push_back({ true,  m.getNoteNumber(), clock + meta.samplePosition, (int) m.getVelocity() }); sawOn = true; }
            else if (m.isNoteOff()) { events.push_back({ false, m.getNoteNumber(), clock + meta.samplePosition, 0 }); }
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

                beginTest("A message-thread edit holding the rhythm lock does not skip blocks or hang a note (" + where + ")");
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

                    expectEquals(p.core.getSkippedBlockCount(), 0);
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
            expectEquals(with.core.sequencer.getRhythm(0).genA.steps, 16);   // the fields wait for the timer too
            r.step();
            expectEquals(with.core.sequencer.getPatternLength(0), stepsBefore);   // and so does the pattern

            with.core.servicePendingRebuild();   // the message thread's timer
            expectEquals(with.core.sequencer.getRhythm(0).genA.steps, 9);
            r.step();
            expect(with.core.sequencer.getPatternLength(0) != stepsBefore, "the pattern was rebuilt");

            // The setter's own notification work is the same with or without our listener; ours adds none.
            expect(coreAllocs <= bareAllocs, "our listener allocated: " + juce::String(coreAllocs) + " vs " + juce::String(bareAllocs));
        }

        beginTest("Audio-thread automation of steps and hits against the timer drain keeps the pattern valid");
        {
            RenderStub stub;
            stub.playing = true;
            juce::AudioBuffer<float> buf(0, 256);
            juce::MidiBuffer midi;

            // "Audio thread": automates steps and hits and renders blocks, a fixed number of times.
            constexpr int kIterations = 20000;
            std::atomic<int> minLen { 1 << 20 }, maxLen { 0 };
            std::atomic<bool> audioDone { false };
            std::thread audio([&]
            {
                for (int i = 0; i < kIterations; ++i)
                {
                    stub.setActual("r0_stepsA", (float) (1 + (i * 7) % 64));
                    stub.setActual("r0_hitsA",  (float) ((i * 13) % 65));
                    midi.clear();
                    stub.processBlock(buf, midi);
                    const int len = stub.core.sequencer.getPatternLength(0);
                    if (len == 0) continue;   // no snapshot yet (the pattern lock was busy on the first block)
                    if (len < minLen.load()) minLen.store(len);
                    if (len > maxLen.load()) maxLen.store(len);
                }
                audioDone.store(true);
            });

            // Message thread: services the timer drain, and checks the rhythm under the rhythm lock.
            int badPatterns = 0, services = 0;
            do
            {
                stub.core.servicePendingRebuild();
                ++services;
                const juce::ScopedLock sl(stub.core.getRhythmLock());
                const auto& g = stub.core.sequencer.getRhythm(0).genA;
                const auto pat = stub.core.sequencer.getRhythm(0).getCombinedPattern();
                int hits = 0;
                for (bool b : pat) hits += b ? 1 : 0;
                if (pat.empty() || pat.size() > 256 || g.steps < 1 || g.steps > 64 || hits > (int) pat.size())
                    ++badPatterns;
            } while (! audioDone.load());
            audio.join();

            stub.core.servicePendingRebuild();
            expectEquals(badPatterns, 0);
            expect(services > 0);
            expect(minLen.load() >= 1 && maxLen.load() <= 256,
                   "pattern length range " + juce::String(minLen.load()) + ".." + juce::String(maxLen.load()));
            expectEquals(stub.core.sequencer.getRhythm(0).genA.steps, 1 + ((kIterations - 1) * 7) % 64);
        }

        beginTest("Two threads setting one parameter to opposite values leave the Rhythm equal to the parameter");
        {
            for (int round = 0; round < 20; ++round)
            {
                RenderStub stub;
                std::atomic<bool> go { false };
                const auto setter = [&](float a, float b)
                {
                    while (! go.load()) std::this_thread::yield();
                    for (int i = 0; i < 2000; ++i)
                        stub.setActual("r0_stepsA", (i & 1) ? a : b);
                };
                std::thread t1(setter, 5.0f, 9.0f), t2(setter, 9.0f, 5.0f);
                go = true;
                t1.join(); t2.join();

                stub.core.servicePendingRebuild();
                expectEquals(stub.core.sequencer.getRhythm(0).genA.steps, (int) stub.actual("r0_stepsA"),
                             "round " + juce::String(round));
                expectEquals(stub.core.rhythmPlayState.stepsA.load(), (int) stub.actual("r0_stepsA"));
            }
        }

        beginTest("The ring step count shows the capped value when a step cap is active");
        {
            RenderStub stub;
            stub.core.sequencer.setStepCap(8);
            stub.setActual("r0_stepsA", 16.0f);
            stub.core.servicePendingRebuild();
            expectEquals(stub.core.rhythmPlayState.stepsA.load(), 8, "after a parameter change");

            RenderStub source;
            source.setActual("r0_stepsA", 20.0f);
            const auto state = session::toXmlString(source.apvts).toStdString();
            stub.setStateInformation(state.data(), (int) state.size());
            expectEquals(stub.core.rhythmPlayState.stepsA.load(), 8, "after a restore");
        }

        beginTest("A 30 Hz stream of dirty parameters never makes the audio thread skip a block");
        {
            RenderStub stub;
            stub.playing = true;
            juce::AudioBuffer<float> buf(0, 256);
            std::atomic<bool> stop { false };
            std::atomic<int> blocks { 0 };

            std::thread audio([&]
            {
                juce::MidiBuffer midi;
                while (! stop.load())
                {
                    midi.clear();
                    stub.processBlock(buf, midi);
                    ++blocks;
                }
            });

            // The message thread: a changed parameter every 33 ms, each followed by its drain.
            for (int i = 0; i < 15; ++i)
            {
                std::thread automation([&] { stub.setActual("r0_stepsA", (float) (8 + i)); });
                automation.join();
                stub.core.servicePendingRebuild();
                juce::Thread::sleep(33);
            }
            stop = true;
            audio.join();

            expect(blocks.load() > 0, "the audio thread ran");
            expectEquals(stub.core.getSkippedBlockCount(), 0);
            expectEquals(stub.core.sequencer.getRhythm(0).genA.steps, 22);
        }
    }
};

static MidiRenderTest midiRenderTest;

//==============================================================================
// What the shipped path plays: the Euclidean generators' patterns on the step grid, the accent
// velocities, and where the host transport puts the pattern.
class MidiPlaybackContentTest : public juce::UnitTest
{
public:
    MidiPlaybackContentTest() : juce::UnitTest("mu-Clid MIDI playback content", "MidiProduct") {}

    static constexpr double kRate  = 44100.0;
    static constexpr int    kBlock = 64;

    // At 120 BPM one 1/16 step is 0.125 s.
    static double stepSamples() { return kRate * 0.125; }

    // The step each note-on landed on: the engine emits it in the first block that starts at or after the step start.
    static std::vector<int> noteSteps(const Renderer& r)
    {
        std::vector<int> steps;
        for (const auto& e : r.events)
            if (e.on) steps.push_back((int) std::floor((double) e.time / stepSamples() + 1.0e-9));
        return steps;
    }

    // Plays steps 0 to 15 of the rhythm as configured by `setup` and returns the steps that sounded.
    template <typename Setup>
    std::vector<int> play(Setup&& setup, std::vector<int>* velocities = nullptr)
    {
        RenderStub p;
        setup(p);
        Renderer r(p, kRate, kBlock);
        p.playing = true;
        r.run(1.99);
        if (velocities != nullptr)
            for (const auto& e : r.events) if (e.on) velocities->push_back(e.velocity);
        expectEquals(r.pairProblem(), juce::String());
        return noteSteps(r);
    }

    static std::vector<int> stepsOf(const std::vector<bool>& pattern, int totalSteps)
    {
        std::vector<int> out;
        for (int s = 0; s < totalSteps; ++s)
            if (pattern[(size_t) s % pattern.size()]) out.push_back(s);
        return out;
    }

    static juce::String describe(const std::vector<int>& v)
    {
        juce::String out;
        for (int x : v) out << x << " ";
        return out;
    }

    void runTest() override
    {
        const juce::ScopedJuceInitialiser_GUI initialiser;

        beginTest("The Euclidean generators hits land on the 1/16 grid as written");
        {
            const auto e48 = play([](RenderStub& p) { p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 4.0f); });
            expect(e48 == (std::vector<int> { 0, 2, 4, 6, 8, 10, 12, 14 }), "E(4,8) over two passes: " + describe(e48));

            const auto all = play([](RenderStub& p) { p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 8.0f); });
            expectEquals((int) all.size(), 16, "E(8,8) sounds every step");

            const auto none = play([](RenderStub& p) { p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 0.0f); });
            expect(none.empty(), "E(0,8) is silent");
        }

        beginTest("Hits, steps and rotation reach the shared generator (the played steps equal its pattern)");
        for (const int rot : { 0, 1, 3, -2 })
            for (const int hits : { 3, 5 })
            {
                const auto played = play([&](RenderStub& p)
                {
                    p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", (float) hits); p.setActual("r0_rotA", (float) rot);
                });
                mu_core::HitGenerator g;
                g.steps = 8; g.hits = hits; g.rotate = rot;
                expect(played == stepsOf(g.getPattern(), 16),
                       "hits " + juce::String(hits) + " rotation " + juce::String(rot));
                expectEquals((int) played.size(), hits * 2);
            }

        beginTest("A step count other than the master loop repeats on its own length");
        {
            const auto played = play([](RenderStub& p) { p.setActual("r0_stepsA", 5.0f); p.setActual("r0_hitsA", 2.0f); });
            mu_core::HitGenerator g;
            g.steps = 5; g.hits = 2;
            expect(played == stepsOf(g.getPattern(), 16));
        }

        beginTest("Accent amount sets the accented and plain velocities");
        for (const float amount : { 0.0f, 50.0f, 100.0f })
        {
            std::vector<int> vel;
            const auto played = play([&](RenderStub& p)
            {
                p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 8.0f);     // every step sounds
                p.setActual("r0_stepsC", 8.0f); p.setActual("r0_hitsC", 2.0f);     // accent on steps 0 and 4
                p.setActual("lite_accentAmt", amount);
            }, &vel);
            expectEquals((int) vel.size(), 16);
            if (vel.size() != 16) continue;
            const auto v = accentVelocities(amount);
            const int accented = (int) std::lround(v.accented * 127.0f), plain = (int) std::lround(v.normal * 127.0f);
            for (size_t i = 0; i < vel.size(); ++i)
            {
                const bool isAccent = (played[i] % 8) == 0 || (played[i] % 8) == 4;
                expect(std::abs(vel[i] - (isAccent ? accented : plain)) <= 1,
                       "accent " + juce::String(amount) + " step " + juce::String(played[i]) + " velocity " + juce::String(vel[i]));
            }
        }

        beginTest("Stopped transport plays nothing; a restart from beat 0 starts on step 0");
        {
            RenderStub p;
            p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 4.0f);
            Renderer r(p, kRate, kBlock);
            r.run(0.5);
            expect(r.events.empty(), "a stopped transport emits nothing");

            p.playing = true;
            r.run(0.6);
            expect(! r.events.empty(), "playing emits notes");
            p.playing = false;
            r.run(0.5);
            const auto afterStop = r.events.size();

            p.hostSample = 0;                    // the host rewinds and presses play
            p.playing = true;
            expect(r.step(), "the first block after a restart from the top sounds step 0");
            expect(r.events.size() > afterStop);
            r.run(0.05);                         // let the note end
            expectEquals(r.pairProblem(), juce::String());
        }

        beginTest("Starting mid-pattern plays from that position, not from the top");
        {
            RenderStub p;
            p.setActual("r0_stepsA", 8.0f); p.setActual("r0_hitsA", 4.0f);       // hits on 0, 2, 4, 6
            Renderer r(p, kRate, kBlock);
            p.hostSample = (juce::int64) (3.5 * stepSamples());                  // the host starts half way through step 3 (a rest)
            p.playing = true;
            expect(! r.step(), "step 3 is a rest, and the pattern is not restarted from its first hit");
            r.run(0.3);
            expect(! r.events.empty());
            if (! r.events.empty())
            {
                const double firstOn = (double) r.events.front().time;
                expect(std::abs(firstOn - 0.5 * stepSamples()) <= kBlock, "the first note is step 4, half a step after the start");
            }
        }

        beginTest("The host tempo sets the step length");
        for (const double bpm : { 90.0, 140.0 })
        {
            RenderStub p;
            p.bpm = bpm;
            p.setActual("r0_stepsA", 4.0f); p.setActual("r0_hitsA", 4.0f);   // every step
            Renderer r(p, kRate, kBlock);
            p.playing = true;
            r.run(2.0);
            const double expectedGap = kRate * 60.0 / bpm / 4.0;
            std::vector<juce::int64> ons;
            for (const auto& e : r.events) if (e.on) ons.push_back(e.time);
            expect(ons.size() > 8);
            for (size_t i = 1; i < ons.size(); ++i)
                expect(std::abs((double) (ons[i] - ons[i - 1]) - expectedGap) <= kBlock,
                       juce::String(bpm) + " BPM gap " + juce::String(ons[i] - ons[i - 1]));
        }
    }
};

static MidiPlaybackContentTest midiPlaybackContentTest;
