// Tests of mu-Toni MIDI's audio path: a bare processor owns the product's real parameter layout and hosts MidiArpCore (the code the
// plugin delegates to), driven block by block with a simulated host transport.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/ParameterLayout.h"
#include "Sequencer/MidiArpCore.h"
#include "Persistence/LayerParamIds.h"

#include <set>
#include <vector>

using namespace mu_toni_midi;

namespace
{

struct Stub : juce::AudioProcessor
{
    Stub() : juce::AudioProcessor(BusesProperties()), apvts(*this, nullptr, juce::Identifier("MuToniMidiState"), createParameterLayout()) {}
    const juce::String getName() const override { return "stub"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

juce::String arp(const char* s) { return mu_toni::layerParamId(0, s); }

struct NoteEvent
{
    juce::int64 at; bool on; int note, velocity, channel;
    bool operator==(const NoteEvent& o) const { return at == o.at && on == o.on && note == o.note && velocity == o.velocity && channel == o.channel; }
};

// The core fed block by block; every note event comes back with its absolute sample.
struct Rig
{
    Stub proc;
    MidiArpCore core { proc.apvts };
    double sr = 48000.0, bpm = 120.0;
    juce::int64 pos = 0;
    std::vector<NoteEvent> events;
    juce::MidiBuffer passed;   // everything that was not a note

    explicit Rig(int block = 256) : blockSize(block) { core.prepare(sr, block); }

    void set(const juce::String& id, float v)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    void holdKey(int note, int channel = 1) { pending.addEvent(juce::MidiMessage::noteOn(channel, note, (juce::uint8) 100), 0); }
    void releaseKey(int note, int channel = 1) { pending.addEvent(juce::MidiMessage::noteOff(channel, note), 0); }
    void sendCC(int cc, int value) { pending.addEvent(juce::MidiMessage::controllerEvent(1, cc, value), 0); }

    void run(double seconds, bool playing)
    {
        const juce::int64 total = (juce::int64) (seconds * sr);
        for (juce::int64 done = 0; done < total; done += blockSize)
        {
            const int n = (int) juce::jmin<juce::int64>(blockSize, total - done);
            juce::MidiBuffer midi;
            midi.addEvents(pending, 0, n, 0);
            pending.clear();
            core.processBlock(playing, (double) pos / sr * bpm / 60.0, bpm, midi, n);
            collect(midi);
            pos += n;
        }
    }

    void bypassBlock()
    {
        juce::MidiBuffer midi;
        core.flushNoteOff(midi);
        collect(midi);
    }

private:
    void collect(const juce::MidiBuffer& midi)
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOnOrOff())
                events.push_back({ pos + meta.samplePosition, m.isNoteOn(), m.getNoteNumber(), (int) m.getVelocity(), m.getChannel() });
            else
                passed.addEvent(m, 0);
        }
    }
    int blockSize;
    juce::MidiBuffer pending;
};

int count(const std::vector<NoteEvent>& e, bool on) { int n = 0; for (auto& x : e) n += x.on == on ? 1 : 0; return n; }

// True when no two notes sound at once and every note-on is closed by a note-off of the same note and channel.
bool balanced(const std::vector<NoteEvent>& e)
{
    int sounding = -1, channel = 0;
    for (auto& x : e)
    {
        if (x.on)
        {
            if (sounding >= 0) { /* legato overlap: the new note is on before the old one's off at the same sample */ }
            sounding = x.note; channel = x.channel;
        }
        else if (x.note == sounding && x.channel == channel) sounding = -1;
    }
    return sounding < 0;
}

} // namespace

struct MidiArpTests : juce::UnitTest
{
    MidiArpTests() : juce::UnitTest("mu-Toni MIDI arp", "mu-toni-midi") {}

    void runTest() override
    {
        beginTest("the parameter layout has the arp rows (the a0_ ids mu-Toni uses) and the three MIDI rows, with sensible defaults");
        {
            Stub s;
            for (const char* id : { "scale", "root", "roct", "chord", "inv", "octs", "dir", "rate", "gate", "leg", "snap", "trig", "acc", "accLen", "accPat", "stepMask" })
                expect(s.apvts.getParameter(arp(id)) != nullptr, juce::String("row ") + id);
            expect(s.apvts.getParameter(kMidiInChId) != nullptr && s.apvts.getParameter(kMidiOutChId) != nullptr && s.apvts.getParameter(kMidiVelId) != nullptr);
            expectEquals(s.apvts.getRawParameterValue(kMidiOutChId)->load(), 1.0f);
            expectEquals(s.apvts.getRawParameterValue(kMidiVelId)->load(), 100.0f);
            expect(s.apvts.getParameter(arp("o1_wt")) == nullptr, "no oscillator rows: it makes no sound");
        }

        beginTest("a held key plays the arp on the transport: 1/16 steps, chord tones of the held root, every note-on closed by its note-off");
        {
            Rig r;
            r.holdKey(48);   // C3: root C, octave 3; the default chord is minor (C Eb G)
            r.run(4.0, true);
            r.run(0.5, false);
            const int ons = count(r.events, true);
            expect(ons >= 31 && ons <= 33, "32 sixteenths in 4 s at 120 bpm, got " + juce::String(ons));
            expectEquals(ons, count(r.events, false));
            expect(balanced(r.events), "no note left sounding");
            std::set<int> pcs;
            for (auto& e : r.events) if (e.on) pcs.insert(e.note % 12);
            expect(pcs == std::set<int>({ 0, 3, 7 }), "only C, Eb and G (got " + juce::String((int) pcs.size()) + " pitch classes)");
            expectEquals((int) r.events.front().at, 0, "first step on beat 0");   // the first step sits on beat 0
        }

        beginTest("nothing plays while the transport is stopped, and the held key is consumed rather than passed on");
        {
            Rig r;
            r.holdKey(60);
            r.sendCC(74, 40);
            r.run(1.0, false);
            expectEquals((int) r.events.size(), 0, "stopped transport plays nothing");
            expectEquals(r.passed.getNumEvents(), 1);   // the CC went through, the note did not
        }

        beginTest("stopping the transport mid-note releases it, and a bypassed block releases a sounding note");
        {
            Rig r;
            r.set(arp("gate"), 100.0f);
            r.set(arp("leg"), 1.0f);   // a 100 % gate with Legato ties notes together: a long note is sounding
            r.holdKey(48);
            r.run(1.0, true);
            expect(count(r.events, true) > count(r.events, false), "a note is sounding");
            r.run(0.2, false);
            expect(balanced(r.events), "the stop ended it");

            Rig b;
            b.set(arp("gate"), 100.0f);
            b.set(arp("leg"), 1.0f);
            b.holdKey(48);
            b.run(0.3, true);
            b.bypassBlock();
            expect(balanced(b.events), "bypass ended it");
        }

        beginTest("the output channel is used, and a note-off still goes to the channel its note-on used when the setting moves");
        {
            Rig r;
            r.set(kMidiOutChId, 5.0f);
            r.holdKey(48);
            r.run(0.05, true);   // the first note is on (it lasts 62 ms)
            r.set(kMidiOutChId, 9.0f);
            r.run(0.1, true);
            expectEquals(r.events.front().channel, 5);
            const auto off = std::find_if(r.events.begin(), r.events.end(), [](const NoteEvent& e) { return ! e.on; });
            expect(off != r.events.end() && off->channel == 5, "the first note-off is on channel 5");
            const auto laterOn = std::find_if(r.events.begin() + 1, r.events.end(), [](const NoteEvent& e) { return e.on; });
            expect(laterOn != r.events.end() && laterOn->channel == 9, "later notes use channel 9");
        }

        beginTest("the MIDI In channel filters the keys: only keys on that channel move the arp's root (Loop mode free-runs on its own root)");
        {
            auto roots = [](const std::vector<NoteEvent>& e)
            {
                std::set<int> pcs;
                for (auto& x : e) if (x.on) pcs.insert(x.note % 12);
                return pcs;
            };
            Rig r;
            r.set(kMidiInChId, 2.0f);
            r.holdKey(49, 1);   // C#, on the wrong channel: ignored
            r.run(0.5, true);
            expect(roots(r.events) == std::set<int>({ 0, 3, 7 }), "the arp stays on its own root C");
            r.events.clear();
            r.holdKey(49, 2);   // the right channel: the root becomes C#
            r.run(0.5, true);
            expect(roots(r.events) == std::set<int>({ 1, 4, 8 }), "channel 2 moves the root to C#");

            Rig omni;   // Omni (the default) hears every channel
            omni.holdKey(49, 7);
            omni.run(0.5, true);
            expect(roots(omni.events) == std::set<int>({ 1, 4, 8 }), "Omni hears channel 7");
        }

        beginTest("MIDI-trigger mode plays only while a key is held");
        {
            Rig r;
            r.set(arp("trig"), 1.0f);
            r.run(0.5, true);
            expectEquals((int) r.events.size(), 0, "trigger mode without a key");
            r.holdKey(48);
            r.run(0.5, true);
            expect(count(r.events, true) > 0, "notes while held");
            r.releaseKey(48);
            r.run(0.5, true);
            const auto before = r.events.size();
            r.run(0.5, true);
            expectEquals((int) r.events.size(), (int) before);   // and silence again once released
            expect(balanced(r.events));
        }

        beginTest("accented steps play at full velocity and the others at the Velocity setting");
        {
            Rig r;
            r.set(arp("acc"), 100.0f);
            r.set(arp("accLen"), 4.0f);
            r.set(arp("accPat"), 1.0f);   // step 1 of 4 accented
            r.set(kMidiVelId, 80.0f);
            r.holdKey(48);
            r.run(1.0, true);
            std::vector<int> vels;
            for (auto& e : r.events) if (e.on) vels.push_back(e.velocity);
            expect(vels.size() >= 8);
            expectEquals(vels[0], 127);
            expectEquals(vels[1], 80);
            expectEquals(vels[4], 127);
        }

        beginTest("the notes do not depend on the host's block size (a step on a block boundary may land one sample early: the shared runner's rounding)");
        {
            Rig a(64), b(480), c(2048);
            for (Rig* r : { &a, &b, &c }) { r->set(arp("rate"), 8.0f); r->holdKey(55); r->run(3.0, true); }
            auto same = [](const std::vector<NoteEvent>& x, const std::vector<NoteEvent>& y)
            {
                if (x.size() != y.size()) return false;
                for (size_t i = 0; i < x.size(); ++i)
                    if (x[i].on != y[i].on || x[i].note != y[i].note || x[i].velocity != y[i].velocity || x[i].channel != y[i].channel
                        || std::abs(x[i].at - y[i].at) > 1) return false;
                return true;
            };
            expect(same(a.events, b.events), "64 vs 480");
            expect(same(a.events, c.events), "64 vs 2048");
            expect(count(a.events, true) > 20);
        }

        beginTest("a modulator aimed at Octaves widens the arp: the same keys, a four-octave sweep instead of one");
        {
            auto span = [](const std::vector<NoteEvent>& e)
            {
                int lo = 127, hi = 0;
                for (auto& x : e) if (x.on) { lo = std::min(lo, x.note); hi = std::max(hi, x.note); }
                return hi - lo;
            };
            Rig plain;
            plain.set(arp("octs"), 1.0f);
            plain.holdKey(48);
            plain.run(3.0, true);

            Rig modulated;
            modulated.set(arp("octs"), 1.0f);
            auto& layer = modulated.core.getLayer();
            auto& cs = layer.controlSequences[0];
            cs.mode = ControlSequence::Mode::Stepped;
            cs.polarity = ControlSequence::Polarity::Unipolar;
            cs.stepValues.assign((size_t) cs.getStepCount(), 100.0f);   // full output on every step
            ModulationAssignment a;
            a.id = "a1"; a.sourceId = "cs0_output"; a.destinationId = "arp.octs"; a.depth = 100.0f;
            expect(layer.modulationMatrix.addAssignment(a), "the assignment is accepted");
            modulated.holdKey(48);
            modulated.run(3.0, true);

            expect(span(plain.events) < 12, "one octave without the modulator (span " + juce::String(span(plain.events)) + ")");
            expect(span(modulated.events) > 24, "the modulator opens it to four (span " + juce::String(span(modulated.events)) + ")");
            expect(balanced(modulated.events));
        }

        beginTest("the modulators round-trip through the saved node, and a target this product has not got is dropped on load");
        {
            Rig a;
            auto& layer = a.core.getLayer();
            auto& cs = layer.controlSequences[0];
            cs.stepValues.assign((size_t) cs.getStepCount(), 50.0f);
            ModulationAssignment keep;
            keep.id = "k1"; keep.sourceId = "cs0_output"; keep.destinationId = "arp.dir"; keep.depth = 40.0f;
            ModulationAssignment synth;   // one of mu-Toni's synth targets: a MIDI effect has no cutoff
            synth.id = "k2"; synth.sourceId = "cs0_output"; synth.destinationId = "flt.cutoff"; synth.depth = 10.0f;
            expect(layer.modulationMatrix.addAssignment(keep) && layer.modulationMatrix.addAssignment(synth));

            juce::ValueTree node("MidiArpLayer");
            layer.writeExtras(node);

            Rig b;
            b.core.getLayer().applyExtras(node);
            const auto& got = b.core.getLayer().modulationMatrix.getAssignments();
            expectEquals((int) got.size(), 1, "only the arp target survives");
            expect(got.size() == 1 && got[0].destinationId == "arp.dir" && std::abs(got[0].depth - 40.0f) < 0.01f);
            expectEquals((int) b.core.getLayer().controlSequences[0].stepValues.size(), (int) cs.stepValues.size());
        }

        beginTest("Legato with a full gate overlaps notes: the next note starts before the last one ends");
        {
            Rig r;
            r.set(arp("gate"), 100.0f);
            r.set(arp("leg"), 1.0f);
            r.holdKey(48);
            r.run(1.0, true);
            // At a step boundary after the first, the new note-on comes before the old note's off.
            int overlaps = 0;
            for (size_t i = 1; i + 1 < r.events.size(); ++i)
                if (r.events[i].on && ! r.events[i + 1].on && r.events[i].at == r.events[i + 1].at && r.events[i + 1].note != r.events[i].note) ++overlaps;
            expect(overlaps >= 4, "legato overlaps: " + juce::String(overlaps));
        }
    }
};

static MidiArpTests midiArpTests;
