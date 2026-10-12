// The per-rhythm Sample / MIDI output mode: the parameter table, the layout defaults, the preset and
// session round trips (an older preset without the parameters loads with their defaults), and a render
// through the real, headless PluginProcessor checking which notes a MIDI-mode rhythm sends, that it
// makes no audio, and that Sample mode still plays its sample and sends no notes.

#include "Plugin/PluginProcessor.h"
#include "Tests/ProcessorSeamHelpers.h"
#include "Audio/MidiOutputMapping.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    using namespace mu_seam_test;

    struct ExposedMidiOutProcessor : mu_clid::PluginProcessor
    {
        using mu_clid::PluginProcessor::drainDeferredWork;
    };

    // One MIDI event seen while rendering.
    struct SeenNote { bool on; int note, channel, velocity; int block, offset; };

    // The result of rendering some blocks: every note event, and the loudest audio sample.
    struct Render
    {
        std::vector<SeenNote> notes;
        float peak = 0.0f;

        int count(bool on) const
        {
            return (int) std::count_if(notes.begin(), notes.end(), [on](const SeenNote& n) { return n.on == on; });
        }
    };

    // Renders `blocks` blocks on the calling thread against a running host transport (120 BPM).
    Render renderBlocks(ExposedMidiOutProcessor& p, int blocks, int blockSize = 512, double sampleRate = 48000.0)
    {
        RunningPlayHead host(sampleRate, blockSize);
        p.setPlayHead(&host);
        Render out;
        juce::AudioBuffer<float> buf(2, blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            p.processBlock(buf, midi);
            out.peak = std::max(out.peak, buf.getMagnitude(0, blockSize));
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn())       out.notes.push_back({ true,  m.getNoteNumber(), m.getChannel(), (int) m.getVelocity(), b, meta.samplePosition });
                else if (m.isNoteOff()) out.notes.push_back({ false, m.getNoteNumber(), m.getChannel(), 0,                       b, meta.samplePosition });
            }
        }
        p.setPlayHead(nullptr);
        return out;
    }

    // A one-second 440 Hz tone, so Sample mode has something audible to play.
    juce::File writeToneWav(const juce::File& dir)
    {
        const int n = 48000;
        juce::AudioBuffer<float> tone(1, n);
        for (int i = 0; i < n; ++i)
            tone.setSample(0, i, 0.8f * std::sin(juce::MathConstants<float>::twoPi * 440.0f * (float) i / 48000.0f));
        auto file = dir.getChildFile("tone.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (stream == nullptr) return {};
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(48000.0)
                                                                                     .withNumChannels(1)
                                                                                     .withBitsPerSample(16));
        if (writer == nullptr) return {};
        writer->writeFromAudioSampleBuffer(tone, 0, n);
        return file;
    }

    const char* const kOutputSuffixes[] = { "outMode", "midiNote", "midiChan", "midiVel", "midiAccent" };
}

struct MidiOutputModeTests : juce::UnitTest
{
    MidiOutputModeTests() : juce::UnitTest("mu-Clid MIDI output mode", "mu-clid-midi-output") {}

    void runTest() override
    {
        tableEntries();
        layoutDefaults();
        velocityMapping();
        renderMidiMode();
        renderSampleModeUnchanged();
        teardownSendsNoteOff();
        presetRoundTrips();
    }

    //==========================================================================
    void tableEntries()
    {
        beginTest("The shared parameter table carries the five output parameters, clamped, as Int");
        const auto* outMode = mu_clid::findRhythmParamDef("outMode");
        const auto* note    = mu_clid::findRhythmParamDef("midiNote");
        const auto* chan    = mu_clid::findRhythmParamDef("midiChan");
        const auto* vel     = mu_clid::findRhythmParamDef("midiVel");
        const auto* accent  = mu_clid::findRhythmParamDef("midiAccent");
        expect(outMode && note && chan && vel && accent, "all five suffixes are in the table");
        if (! (outMode && note && chan && vel && accent)) return;

        for (const auto* d : { outMode, note, chan, vel, accent })
            expect(d->kind == mu_clid::ParamKind::Int, juce::String(d->suffix) + " is an Int kind");

        mu_clid::Rhythm r;
        bool pd = false, vd = false;
        expectEquals(outMode->push(r), 0.0f);   // the defaults
        expectEquals(note->push(r), 36.0f);
        expectEquals(chan->push(r), 1.0f);
        expectEquals(vel->push(r), 100.0f);
        expectEquals(accent->push(r), 0.0f);

        outMode->apply(7.0f, r, pd, vd);    expectEquals(outMode->push(r), 1.0f);
        outMode->apply(-3.0f, r, pd, vd);   expectEquals(outMode->push(r), 0.0f);
        note->apply(200.0f, r, pd, vd);     expectEquals(note->push(r), 127.0f);
        note->apply(61.0f, r, pd, vd);      expectEquals(note->push(r), 61.0f);
        chan->apply(0.0f, r, pd, vd);       expectEquals(chan->push(r), 1.0f);
        chan->apply(99.0f, r, pd, vd);      expectEquals(chan->push(r), 16.0f);
        vel->apply(0.0f, r, pd, vd);        expectEquals(vel->push(r), 1.0f);
        vel->apply(500.0f, r, pd, vd);      expectEquals(vel->push(r), 100.0f);
        accent->apply(-5.0f, r, pd, vd);    expectEquals(accent->push(r), 0.0f);
        accent->apply(250.0f, r, pd, vd);   expectEquals(accent->push(r), 100.0f);
        expect(! pd && ! vd, "none of them asks for a pattern rebuild or a voice resync");

        // A rhythm copy (swap, hot-swap, reset) carries the settings.
        mu_clid::Rhythm src;
        note->apply(72.0f, src, pd, vd);
        outMode->apply(1.0f, src, pd, vd);
        mu_clid::Rhythm dst = src;
        expectEquals(note->push(dst), 72.0f);
        expectEquals(outMode->push(dst), 1.0f);
    }

    void layoutDefaults()
    {
        beginTest("Every rhythm slot declares the output parameters with the default Sample / C1 / ch 1 / 100 % / 0 %");
        TempStorage storage;
        ExposedMidiOutProcessor p;
        for (int ri = 0; ri < mu_clid::RhythmSequencer::MaxRhythms; ++ri)
        {
            const auto prefix = "r" + juce::String(ri) + "_";
            for (const char* s : kOutputSuffixes)
                expect(p.apvts.getParameter(prefix + s) != nullptr, prefix + s + " exists");
            expectEquals(getParam(p.apvts, prefix + "outMode"), 0.0f);
            expectEquals(getParam(p.apvts, prefix + "midiNote"), 36.0f);
            expectEquals(getParam(p.apvts, prefix + "midiChan"), 1.0f);
            expectEquals(getParam(p.apvts, prefix + "midiVel"), 100.0f);
            expectEquals(getParam(p.apvts, prefix + "midiAccent"), 0.0f);
        }
        // The ranges.
        auto range = [&](const char* id) { auto* prm = p.apvts.getParameter(id); return prm->convertFrom0to1(1.0f); };
        auto low   = [&](const char* id) { auto* prm = p.apvts.getParameter(id); return prm->convertFrom0to1(0.0f); };
        expectEquals(range("r0_outMode"), 1.0f);     expectEquals(low("r0_outMode"), 0.0f);
        expectEquals(range("r0_midiNote"), 127.0f);  expectEquals(low("r0_midiNote"), 0.0f);
        expectEquals(range("r0_midiChan"), 16.0f);   expectEquals(low("r0_midiChan"), 1.0f);
        expectEquals(range("r0_midiVel"), 100.0f);   expectEquals(low("r0_midiVel"), 1.0f);
        expectEquals(range("r0_midiAccent"), 100.0f); expectEquals(low("r0_midiAccent"), 0.0f);

        // The Rhythm struct starts on the same defaults as the parameters, and the processor's default
        // rhythm carries them.
        const auto& r = p.getRhythm(0);
        expectEquals(r.outputMode.load(), 0);
        expectEquals(r.midiNote.load(), 36);
        expectEquals(r.midiChannel.load(), 1);
        expectEquals(r.midiVelocity.load(), 100);
        expectEquals(r.midiAccent.load(), 0);
    }

    void velocityMapping()
    {
        beginTest("midiOutVelocity: Accent shape scaled by the Velocity setting, never below 1");
        // Defaults (vel 100 %, accent 0): both a plain and an accented hit are velocity 100.
        expectWithinAbsoluteError(mu_core::midiOutVelocity(100, 0.0f, false) * 127.0f, 100.0f, 0.01f);
        expectWithinAbsoluteError(mu_core::midiOutVelocity(100, 0.0f, true)  * 127.0f, 100.0f, 0.01f);
        // Accent 100: accented 127, plain 75.
        expectWithinAbsoluteError(mu_core::midiOutVelocity(100, 100.0f, true)  * 127.0f, 127.0f, 0.01f);
        expectWithinAbsoluteError(mu_core::midiOutVelocity(100, 100.0f, false) * 127.0f, 75.0f, 0.01f);
        // Velocity 50 % halves them.
        expectWithinAbsoluteError(mu_core::midiOutVelocity(50, 100.0f, true) * 127.0f, 63.5f, 0.01f);
        // The floor keeps the engine's truncation from reaching 0 (a note-on at 0 is a note-off).
        expect((int) (mu_core::midiOutVelocity(1, 100.0f, false) * 127.0f) >= 1);
        expect(mu_core::midiOutVelocity(1000, 100.0f, true) <= 1.0f);
        expectEquals(mu_core::midiNoteName(60), juce::String("C4"));
        expectEquals(mu_core::midiNoteName(0), juce::String("C-1"));
        expectEquals(mu_core::midiNoteName(127), juce::String("G9"));
    }

    //==========================================================================
    // The default rhythm (16 steps, 4 hits) on a running 120 BPM transport hits every quarter note.
    void renderMidiMode()
    {
        beginTest("A MIDI-mode rhythm sends note-on/off with the set note, channel and velocity, and makes no audio");
        {
            TempStorage storage;
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);
            const auto wav = writeToneWav(storage.root);
            p.samples.load(0, wav);
            expect(! p.samples.isSampleMissing(0), "the tone sample loaded");

            setParam(p.apvts, "r0_outMode", 1.0f);
            setParam(p.apvts, "r0_midiNote", 50.0f);
            setParam(p.apvts, "r0_midiChan", 3.0f);
            setParam(p.apvts, "r0_midiVel", 80.0f);
            p.drainDeferredWork();
            expectEquals(p.getRhythm(0).outputMode.load(), 1);

            const auto r = renderBlocks(p, 190);   // about 2 s: 4 quarter-note hits
            expect(r.count(true) >= 3, "note-ons were sent (got " + juce::String(r.count(true)) + ")");
            for (const auto& n : r.notes)
            {
                expectEquals(n.note, 50);
                expectEquals(n.channel, 3);
                if (n.on)
                    expect(std::abs(n.velocity - 80) <= 1, "velocity 80 % of 100 is about 80 (got " + juce::String(n.velocity) + ")");
            }
            expect(std::abs(r.count(true) - r.count(false)) <= 1, "every note-on gets its note-off");
            expect(r.peak < 1.0e-6f, "no audio from this rhythm (peak " + juce::String(r.peak) + ")");

            // The note-off follows 20 ms (960 samples at 48 kHz) after its note-on.
            for (size_t i = 0; i + 1 < r.notes.size(); ++i)
                if (r.notes[i].on && ! r.notes[i + 1].on)
                {
                    const int on  = r.notes[i].block * 512 + r.notes[i].offset;
                    const int off = r.notes[i + 1].block * 512 + r.notes[i + 1].offset;
                    expect(std::abs((off - on) - 960) <= 512, "note length is about 20 ms (got " + juce::String(off - on) + " samples)");
                }
        }

        beginTest("Accent: with Ring C coincident the accented hits are louder, the plain ones softer");
        {
            TempStorage storage;
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);
            // Ring A: 4 hits in 16 steps (0, 4, 8, 12); Ring C: 2 hits in 16 steps, so half the hits are accented.
            setParam(p.apvts, "r0_stepsC", 16.0f);
            setParam(p.apvts, "r0_hitsC", 2.0f);
            setParam(p.apvts, "r0_outMode", 1.0f);
            setParam(p.apvts, "r0_midiAccent", 100.0f);
            p.drainDeferredWork();

            const auto r = renderBlocks(p, 380);   // about 4 s: two full bars of the pattern
            std::vector<int> velocities;
            for (const auto& n : r.notes) if (n.on) velocities.push_back(n.velocity);
            expect(velocities.size() >= 6, "enough hits (got " + juce::String((int) velocities.size()) + ")");
            const auto hi = std::count_if(velocities.begin(), velocities.end(), [](int v) { return std::abs(v - 127) <= 1; });
            const auto lo = std::count_if(velocities.begin(), velocities.end(), [](int v) { return std::abs(v - 75) <= 1; });
            expect(hi > 0, "accented hits reach 127");
            expect(lo > 0, "plain hits drop to 75");
            expectEquals((int) (hi + lo), (int) velocities.size());
        }

        beginTest("Switching a rhythm from MIDI back to Sample plays the sample again and stops the notes");
        {
            TempStorage storage;
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);
            p.samples.load(0, writeToneWav(storage.root));
            setParam(p.apvts, "r0_outMode", 1.0f);
            p.drainDeferredWork();
            const auto midiRun = renderBlocks(p, 100);
            expect(midiRun.count(true) >= 1 && midiRun.peak < 1.0e-6f);

            setParam(p.apvts, "r0_outMode", 0.0f);
            p.drainDeferredWork();
            const auto sampleRun = renderBlocks(p, 100);
            expect(sampleRun.peak > 1.0e-3f, "the sample plays again");
            expectEquals(sampleRun.count(true), 0);
        }
    }

    void renderSampleModeUnchanged()
    {
        beginTest("Sample mode (the default) plays the sample and sends no MIDI notes");
        TempStorage storage;
        ExposedMidiOutProcessor p;
        p.prepareToPlay(48000.0, 512);
        p.samples.load(0, writeToneWav(storage.root));
        const auto r = renderBlocks(p, 190);
        expect(r.peak > 1.0e-3f, "audible (peak " + juce::String(r.peak) + ")");
        expectEquals((int) r.notes.size(), 0);

        // A MIDI-mode rhythm beside a Sample-mode one: only the Sample one sounds, only the MIDI one sends.
        beginTest("Two rhythms: the Sample one sounds, the MIDI one sends its own note");
        TempStorage storage2;
        ExposedMidiOutProcessor q;
        q.prepareToPlay(48000.0, 512);
        q.rhythms.add(mu_clid::Rhythm());
        q.applyDefaultRhythm(1);
        setParam(q.apvts, "r1_stepsA", 16.0f);   // a new rhythm starts with no hits
        setParam(q.apvts, "r1_hitsA", 4.0f);
        setParam(q.apvts, "r1_outMode", 1.0f);
        setParam(q.apvts, "r1_midiNote", 60.0f);
        q.drainDeferredWork();
        const auto two = renderBlocks(q, 190);   // rhythm 0 has no sample: silent either way
        expect(two.count(true) >= 3, "rhythm 1 sends notes");
        for (const auto& n : two.notes) expectEquals(n.note, 60);
    }

    //==========================================================================
    // A MIDI-mode slot torn down inside its 20 ms hold must still send its note-off.
    static bool everyNoteOnHasOff(const Render& a, const Render& b)
    {
        std::vector<SeenNote> all = a.notes;
        all.insert(all.end(), b.notes.begin(), b.notes.end());
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].on)
            {
                bool matched = false;
                for (size_t j = i + 1; j < all.size() && ! matched; ++j)
                    matched = ! all[j].on && all[j].note == all[i].note && all[j].channel == all[i].channel;
                if (! matched) return false;
            }
        return true;
    }

    void teardownSendsNoteOff()
    {
        beginTest("Removing a MIDI-mode slot inside its 20 ms hold still sends the note-off (note and channel match)");
        {
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);
            p.rhythms.add(mu_clid::Rhythm());
            p.applyDefaultRhythm(1);
            setParam(p.apvts, "r1_stepsA", 16.0f);
            setParam(p.apvts, "r1_hitsA", 4.0f);
            setParam(p.apvts, "r1_outMode", 1.0f);
            setParam(p.apvts, "r1_midiNote", 61.0f);
            setParam(p.apvts, "r1_midiChan", 5.0f);
            p.drainDeferredWork();

            const auto first = renderBlocks(p, 1);   // the hit at beat 0: note-on, its off is 960 samples away
            expectEquals(first.count(true), 1);
            expectEquals(first.count(false), 0);
            p.rhythms.remove(1);
            expectEquals(p.getNumRhythms(), 1);
            const auto second = renderBlocks(p, 1);
            expect(everyNoteOnHasOff(first, second), "the removed slot's note-on got its note-off");
            expectEquals(second.count(false), 1);
            expectEquals(second.notes.empty() ? -1 : second.notes[0].offset, 0);
        }

        beginTest("A session restore that shrinks the rhythm count inside the hold still sends the note-off");
        {
            juce::MemoryBlock oneRhythm;
            {
                ExposedMidiOutProcessor src;
                src.prepareToPlay(48000.0, 512);
                src.getStateInformation(oneRhythm);
            }
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);
            p.rhythms.add(mu_clid::Rhythm());
            p.applyDefaultRhythm(1);
            setParam(p.apvts, "r1_stepsA", 16.0f);
            setParam(p.apvts, "r1_hitsA", 4.0f);
            setParam(p.apvts, "r1_outMode", 1.0f);
            setParam(p.apvts, "r1_midiNote", 62.0f);
            setParam(p.apvts, "r1_midiChan", 9.0f);
            p.drainDeferredWork();

            const auto first = renderBlocks(p, 1);
            expectEquals(first.count(true), 1);
            p.setStateInformation(oneRhythm.getData(), (int) oneRhythm.getSize());
            p.drainDeferredWork();
            expectEquals(p.getNumRhythms(), 1);
            const auto second = renderBlocks(p, 1);
            expect(everyNoteOnHasOff(first, second), "the shrunk slot's note-on got its note-off");
        }
    }

    //==========================================================================
    void presetRoundTrips()
    {
        beginTest("A rhythm preset round-trips the output settings; an older preset without them loads the defaults");
        {
            TempStorage storage;
            ExposedMidiOutProcessor p;
            p.prepareToPlay(48000.0, 512);

            setParam(p.apvts, "r0_outMode", 1.0f);
            setParam(p.apvts, "r0_midiNote", 61.0f);
            setParam(p.apvts, "r0_midiChan", 10.0f);
            setParam(p.apvts, "r0_midiVel", 55.0f);
            setParam(p.apvts, "r0_midiAccent", 33.0f);
            p.drainDeferredWork();
            p.saveLayerPreset(0, "midiOut");
            const auto file = p.getRhythmsDir().getChildFile("midiOut.muRhythm");
            expect(file.existsAsFile(), "the preset was written");
            const auto xml = file.loadFileAsString();
            for (const char* s : kOutputSuffixes)
                expect(xml.contains(juce::String("r0_") + s + "="), juce::String("the preset carries r0_") + s);

            // Back to the defaults, then load: the saved settings return.
            for (const char* s : kOutputSuffixes)
                if (auto* prm = p.apvts.getParameter(juce::String("r0_") + s))
                    setParam(p.apvts, juce::String("r0_") + s, prm->convertFrom0to1(prm->getDefaultValue()));
            p.drainDeferredWork();
            expectEquals(p.getRhythm(0).outputMode.load(), 0);
            p.stageRhythmPreset(0, file);
            p.drainDeferredWork();
            expectEquals(getParam(p.apvts, "r0_outMode"), 1.0f);
            expectEquals(getParam(p.apvts, "r0_midiNote"), 61.0f);
            expectEquals(getParam(p.apvts, "r0_midiChan"), 10.0f);
            expectEquals(getParam(p.apvts, "r0_midiVel"), 55.0f);
            expectEquals(getParam(p.apvts, "r0_midiAccent"), 33.0f);
            expectEquals(p.getRhythm(0).midiNote.load(), 61);
            expectEquals(p.getRhythm(0).outputMode.load(), 1);

            // An older preset: the same file with the five properties removed. Loading it over a rhythm
            // that is in MIDI mode must give the defaults.
            juce::String older = xml;
            for (const char* s : kOutputSuffixes)
            {
                const juce::String key = juce::String(" r0_") + s + "=\"";
                const int at = older.indexOf(key);
                if (at >= 0)
                {
                    const int end = older.indexOf(at + key.length(), "\"");
                    older = older.substring(0, at) + older.substring(end + 1);
                }
            }
            for (const char* s : kOutputSuffixes)
                expect(! older.contains(juce::String("r0_") + s + "="), juce::String("stripped r0_") + s);
            const auto olderFile = p.getRhythmsDir().getChildFile("older.muRhythm");
            expect(olderFile.replaceWithText(older), "wrote the older preset");

            p.stageRhythmPreset(0, olderFile);
            p.drainDeferredWork();
            expectEquals(getParam(p.apvts, "r0_outMode"), 0.0f);
            expectEquals(getParam(p.apvts, "r0_midiNote"), 36.0f);
            expectEquals(getParam(p.apvts, "r0_midiChan"), 1.0f);
            expectEquals(getParam(p.apvts, "r0_midiVel"), 100.0f);
            expectEquals(getParam(p.apvts, "r0_midiAccent"), 0.0f);
            expectEquals(p.getRhythm(0).outputMode.load(), 0);
        }

        beginTest("A host session round-trips the output settings of every rhythm, including a second one");
        {
            juce::MemoryBlock state;
            {
                TempStorage storage;
                ExposedMidiOutProcessor p;
                p.prepareToPlay(48000.0, 512);
                p.rhythms.add(mu_clid::Rhythm());
                p.applyDefaultRhythm(1);
                setParam(p.apvts, "r0_outMode", 1.0f);
                setParam(p.apvts, "r0_midiNote", 45.0f);
                setParam(p.apvts, "r1_midiChan", 7.0f);
                setParam(p.apvts, "r1_midiAccent", 20.0f);
                p.drainDeferredWork();
                p.getStateInformation(state);
            }
            TempStorage storage;
            ExposedMidiOutProcessor q;
            q.prepareToPlay(48000.0, 512);
            q.setStateInformation(state.getData(), (int) state.getSize());
            q.drainDeferredWork();
            expectEquals(getParam(q.apvts, "r0_outMode"), 1.0f);
            expectEquals(getParam(q.apvts, "r0_midiNote"), 45.0f);
            expectEquals(getParam(q.apvts, "r1_midiChan"), 7.0f);
            expectEquals(getParam(q.apvts, "r1_midiAccent"), 20.0f);
            expectEquals(q.getRhythm(0).outputMode.load(), 1);
            expectEquals(q.getRhythm(0).midiNote.load(), 45);
            expectEquals(q.getNumRhythms(), 2);
            expectEquals(q.getRhythm(1).midiChannel.load(), 7);
        }
    }
};

static MidiOutputModeTests midiOutputModeTests;
