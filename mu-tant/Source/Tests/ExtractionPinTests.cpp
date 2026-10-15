// Layer-extraction safety net for mu-Tant, part 2: pins. Each test describes what the unextracted tree
// does TODAY about something a later stage will move: Note mode (a held-note stack and a gate that
// multiplies the FINAL mix, so it also gates the FX return and every voice), the 25 ms retire fade of
// a count-reducing swap (including the stale tail that survives a quick regrow), a structural edit
// mid-play, and a block larger than the prepared size. Written and passing on the unmodified tree; a
// stage that moves the behaviour must pass them unedited, and a stage that has to edit one has changed
// behaviour, which goes to Andy.
//
// Part 1, the bit-exact golden renders, is GoldenRenderTests.cpp.

#include "GoldenTestRig.h"
#include "Tests/GoldenRender.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    using Buf = juce::AudioBuffer<float>;
    using mu_golden_rig::OwnedProcessor;
    using mu_golden_rig::openGate;
    using mu_golden_rig::setParam;
    using mu_golden_rig::setVoice;

    constexpr double kSr = 48000.0;
    constexpr int kRampSamples = 384;       // the Note-mode gate fade: 8 ms at 48 kHz
    constexpr int kRetireSamples = 1200;    // the retire fade of a count-reducing swap: 25 ms at 48 kHz

    float peakOf(const Buf& b, int from, int to)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            p = juce::jmax(p, b.getMagnitude(c, from, juce::jmax(0, to - from)));
        return p;
    }

    float rmsOf(const Buf& b, int from, int to)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = from; i < to; ++i) { const double v = b.getSample(c, i); sum += v * v; }
        return (float) std::sqrt(sum / (double) juce::jmax(1, (to - from) * b.getNumChannels()));
    }

    // Positive-going zero crossings per second on channel 0 in [from, to): the pitch of a clean tone.
    double crossingHz(const Buf& b, int from, int to)
    {
        int n = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample(0, i - 1) <= 0.0f && b.getSample(0, i) > 0.0f) ++n;
        return (double) n * kSr / (double) juce::jmax(1, to - from);
    }

    // Index of the last non-zero sample on channel 0, or -1.
    int lastNonZero(const Buf& b)
    {
        for (int i = b.getNumSamples() - 1; i >= 0; --i)
            if (b.getSample(0, i) != 0.0f) return i;
        return -1;
    }

    // A constant-valued wavetable: the oscillator output is the same whatever the phase or pitch, so the
    // render is a level that gates and fades can be read straight off.
    juce::File writeDcTable(const juce::File& dir)
    {
        const int frames = 4, frameSize = 2048;
        Buf data(1, frames * frameSize);
        for (int i = 0; i < data.getNumSamples(); ++i) data.setSample(0, i, 0.5f);
        auto file = dir.getChildFile("dc.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
        if (out == nullptr) return file;
        auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(kSr)
                                                   .withNumChannels(1).withBitsPerSample(16));
        if (writer != nullptr) writer->writeFromAudioSampleBuffer(data, 0, data.getNumSamples());
        return file;
    }

    // A real PluginProcessor with a mock host, prepared, driven block by block on the test thread.
    struct Rig
    {
        explicit Rig(int maxBlock = 512) : maxBlockSize(maxBlock)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(kSr, maxBlock);
            p.prepareToPlay(kSr, maxBlock);
            dc = writeDcTable(p.storage.root);
        }
        ~Rig() { p.setPlayHead(nullptr); p.releaseResources(); }

        // One host block of `n` frames (0 is allowed); returns what the processor wrote.
        Buf block(int n, juce::MidiBuffer midi = {})
        {
            Buf work(2, juce::jmax(1, n));
            work.clear();
            Buf view(work.getArrayOfWritePointers(), 2, n);
            host.timeInSamples = frame;
            p.processBlock(view, midi);
            if (host.playing) host.ppq += (double) n * host.bpm / (60.0 * kSr);
            frame += n;
            Buf out(2, n);
            for (int c = 0; c < 2; ++c) out.copyFrom(c, 0, view, c, 0, n);
            return out;
        }

        // `frames` of audio in blocks of `blockSize`, joined.
        Buf render(int frames, int blockSize = 512)
        {
            Buf all(2, frames);
            for (int at = 0; at < frames; at += blockSize)
            {
                const int n = juce::jmin(blockSize, frames - at);
                const auto b = block(n);
                for (int c = 0; c < 2; ++c) all.copyFrom(c, at, b, c, 0, n);
            }
            return all;
        }

        // A voice that is a pure level: the DC table, gater open, no second oscillator or noise.
        void dcVoice(int v, float levelDb = -6.0f)
        {
            p.loadUserWavetable(v, 0, dc);
            setVoice(p.apvts, v, "o1_lvl", 0.0f);
            setVoice(p.apvts, v, "o2_lvl", -60.0f);
            setVoice(p.apvts, v, "flt_res", 0.0f);
            setVoice(p.apvts, v, "level", levelDb);
            openGate(p.apvts, v);
        }

        // A host or saved-state blob of the processor as it is now with `voices` voices: what a DAW hands
        // setStateInformation when a project reloads or an undo step is restored.
        juce::MemoryBlock blob(int voices)
        {
            auto tree = p.captureFullPreset();
            tree.setProperty("numVoices", voices, nullptr);
            juce::MemoryBlock mb;
            if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
            return mb;
        }
        void restore(const juce::MemoryBlock& mb) { p.setStateInformation(mb.getData(), (int) mb.getSize()); }

        // Only strip `v` reaches the output, with its sends off: the other strips among the first `count` are
        // muted. (The mixer's solo is not used: it ignores a soloed strip that is no longer active, which a
        // dropped voice is, and then lets the others through.)
        void isolate(int v, int count)
        {
            for (int c = 0; c < count; ++c)
                if (c != v) setParam(p.apvts, "ch" + juce::String(c) + "_mute", 1.0f);
            for (const char* s : { "sendEff", "sendDly", "sendRev" }) setParam(p.apvts, "ch" + juce::String(v) + "_" + s, 0.0f);
        }

        OwnedProcessor p;
        mu_golden::MockHost host;
        juce::File dc;
        int maxBlockSize;
        juce::int64 frame = 0;
    };

    juce::MidiBuffer midiOf(std::initializer_list<juce::MidiMessage> msgs, int offset = 0)
    {
        juce::MidiBuffer mb;
        for (const auto& m : msgs) mb.addEvent(m, offset);
        return mb;
    }
    juce::MidiMessage on(int note)  { return juce::MidiMessage::noteOn(1, note, (juce::uint8) 100); }
    juce::MidiMessage off(int note) { return juce::MidiMessage::noteOff(1, note); }
}

struct MuTantExtractionPinTests : juce::UnitTest
{
    MuTantExtractionPinTests() : juce::UnitTest("mu-Tant extraction pins", "mu-tant-pins") {}

    void runTest() override
    {
        noteModeGatePins();
        noteModeFinalMixPins();
        noteModeHeldNotePins();
        retirePins();
        staleRetirePins();
        structuralEditPins();
        oversizeBlockPins();
    }

    // ── Note mode: the gate ramp ───────────────────────────────────────────────────────────────
    void noteModeGatePins()
    {
        beginTest("Note mode: the gate starts shut, opens over 8 ms from the FIRST sample of the block holding the note-on, closes the same way");
        Rig rig;
        rig.dcVoice(0);
        rig.p.setMidiNoteMode(1);

        const auto idle = rig.render(6 * 512);
        expectEquals(peakOf(idle, 0, idle.getNumSamples()), 0.0f, "Note mode with no note held is silent, exactly");

        // The note-on sits at offset 300 of its block, but the held state is read per block, so the ramp
        // starts at sample 0.
        const auto opening = rig.block(512, midiOf({ on(60) }, 300));
        rig.render(4 * 512);
        const auto steadyBlock = rig.block(512);
        const float steady = steadyBlock.getSample(0, 511);
        expect(steady > 0.01f, "the held note sounds");

        float worstOpen = 0.0f;
        for (int i = 0; i < 512; ++i)
            worstOpen = juce::jmax(worstOpen, std::abs(opening.getSample(0, i) / steady - juce::jmin(1.0f, (float) (i + 1) / (float) kRampSamples)));
        logMessage("gate open ramp: steady " + juce::String(steady, 5) + ", y(0)/steady " + juce::String(opening.getSample(0, 0) / steady, 5)
                   + ", worst deviation from a linear 384-sample ramp " + juce::String(worstOpen, 5));
        expect(worstOpen < 0.01f, "the gate opens linearly over 384 samples from the first sample of the block");
        expect(opening.getSample(0, 0) > 0.0f && opening.getSample(0, 0) < 0.01f * steady * 4.0f, "so the first sample is already above zero, before the note's own offset");

        // The release: the note-off at offset 400 closes the gate from the start of its block.
        const auto closing = rig.block(512, midiOf({ off(60) }, 400));
        float worstClose = 0.0f;
        for (int i = 0; i < 512; ++i)
            worstClose = juce::jmax(worstClose, std::abs(closing.getSample(0, i) / steady - juce::jmax(0.0f, 1.0f - (float) (i + 1) / (float) kRampSamples)));
        logMessage("gate close ramp: worst deviation " + juce::String(worstClose, 5) + ", last sample " + juce::String(closing.getSample(0, 511), 8));
        expect(worstClose < 0.01f, "and closes linearly over 384 samples");
        expectEquals(peakOf(closing, kRampSamples, 512), 0.0f, "fully shut from sample 384 on, exactly zero");
        expectEquals(peakOf(rig.render(2 * 512), 0, 1024), 0.0f, "and it stays shut");
    }

    // ── Note mode: the gate multiplies the FINAL mix ───────────────────────────────────────────
    void noteModeFinalMixPins()
    {
        // A drone with generous delay and reverb sends: the FX return outlasts the note by seconds.
        auto fxVoice = [](Rig& r, float send)
        {
            r.p.addLayer();
            for (int v = 0; v < 2; ++v) { openGate(r.p.apvts, v); setVoice(r.p.apvts, v, "o1_wt", 1); setVoice(r.p.apvts, v, "flt_cut", 3000); }
            setVoice(r.p.apvts, 1, "o1_oct", 1);
            setParam(r.p.apvts, "ch0_sendRev", send); setParam(r.p.apvts, "ch0_sendDly", send);
            setParam(r.p.apvts, "ch1_sendRev", send);
            setParam(r.p.apvts, "ret_rev_lvl", 1.0f); setParam(r.p.apvts, "ret_dly_lvl", 0.9f);
            setParam(r.p.apvts, "rev_size", 0.9f);    setParam(r.p.apvts, "dly_fb", 0.6f);
        };

        beginTest("Note mode: after the release the FINAL mix is exactly silent although the FX return would ring on (the gate multiplies the mix, not the voices)");
        {
            Rig note;
            fxVoice(note, 0.9f);
            note.p.setMidiNoteMode(1);
            note.render(2 * 512);
            note.block(512, midiOf({ on(60) }));
            const auto held = note.render(30 * 512);
            const float heldRms = rmsOf(held, 20 * 512, 30 * 512);
            note.block(512, midiOf({ off(60) }));
            const auto after = note.render(60 * 512);

            // The same patch in Free mode, the drones cut by closing the gaters instead of releasing a note:
            // the FX return keeps ringing, which is what an ungated final mix carries.
            Rig freeMode;
            fxVoice(freeMode, 0.9f);
            freeMode.render(2 * 512 + 512 + 30 * 512);
            setVoice(freeMode.p.apvts, 0, "gate_bypass", 0.0f);
            setVoice(freeMode.p.apvts, 1, "gate_bypass", 0.0f);
            freeMode.block(512);
            const auto freeAfter = freeMode.render(60 * 512);

            logMessage("held (2 voices + sends) rms " + juce::String(heldRms, 5) + "; 0.21 s to 0.64 s after the release: Note mode peak "
                       + juce::String(peakOf(after, 20 * 512, 60 * 512), 8) + ", Free mode with the drones cut peak "
                       + juce::String(peakOf(freeAfter, 20 * 512, 60 * 512), 6));
            expect(heldRms > 0.01f, "the held note sounds, with both voices and the returns");
            expectEquals(peakOf(after, 0, after.getNumSamples()), 0.0f, "Note mode: the final mix is exactly silent once the 8 ms fade has run");
            expect(peakOf(freeAfter, 20 * 512, 60 * 512) > 1.0e-4f, "Free mode: the same sends keep ringing after the drones stop, so the Note-mode gate is what silences the FX return");
        }

        beginTest("Note mode: the stem (sends 0) and the final mix with sends both close; with a note held the sends add to the mix");
        {
            Rig withFx, noFx;
            fxVoice(withFx, 0.9f);
            fxVoice(noFx, 0.0f);
            for (auto* r : { &withFx, &noFx }) r->p.setMidiNoteMode(1);
            for (auto* r : { &withFx, &noFx }) { r->render(2 * 512); r->block(512, midiOf({ on(60) })); }
            const auto a = withFx.render(30 * 512), b = noFx.render(30 * 512);
            const float rmsFx = rmsOf(a, 20 * 512, 30 * 512), rmsDry = rmsOf(b, 20 * 512, 30 * 512);
            logMessage("held: final mix rms with sends " + juce::String(rmsFx, 5) + ", dry stem rms " + juce::String(rmsDry, 5));
            expect(std::abs(rmsFx - rmsDry) > 0.02f * rmsDry, "the FX return is audible in the final mix while the note is held");
            for (auto* r : { &withFx, &noFx }) r->block(512, midiOf({ off(60) }));
            expectEquals(peakOf(withFx.render(10 * 512), 0, 10 * 512), 0.0f, "final mix with sends is shut");
            expectEquals(peakOf(noFx.render(10 * 512), 0, 10 * 512), 0.0f, "the dry stem is shut");
        }

        beginTest("Note mode: one note gates every voice together (the gate sits after the sum of all layers)");
        {
            Rig rig;
            rig.p.addLayer();
            rig.dcVoice(0); rig.dcVoice(1);
            rig.p.setMidiNoteMode(1);
            rig.render(2 * 512);
            rig.block(512, midiOf({ on(64) }));
            const auto both = rig.render(8 * 512);
            const float sum = both.getSample(0, both.getNumSamples() - 1);

            Rig one;
            one.dcVoice(0);
            one.p.setMidiNoteMode(1);
            one.render(2 * 512);
            one.block(512, midiOf({ on(64) }));
            const auto single = one.render(8 * 512);
            const float lone = single.getSample(0, single.getNumSamples() - 1);
            logMessage("one note, two voices: " + juce::String(sum, 5) + "; one voice: " + juce::String(lone, 5));
            expectWithinAbsoluteError(sum, 2.0f * lone, 0.01f * std::abs(lone));

            rig.block(512, midiOf({ off(64) }));
            expectEquals(peakOf(rig.render(4 * 512), 0, 4 * 512), 0.0f, "both voices close on the one release");
        }
    }

    // ── Note mode: the held-note stack and the pitch track ─────────────────────────────────────
    void noteModeHeldNotePins()
    {
        beginTest("Note mode: the last note held sets the pitch (transposed from the tonal centre), a released top note falls back to the one below");
        Rig rig;
        setVoice(rig.p.apvts, 0, "o1_wt", 0);     setVoice(rig.p.apvts, 0, "o2_lvl", -60);
        setVoice(rig.p.apvts, 0, "flt_cut", 12000); setVoice(rig.p.apvts, 0, "flt_res", 0.0f);
        openGate(rig.p.apvts, 0);
        rig.p.setMidiNoteMode(1);
        rig.render(2 * 512);

        auto pitchAfter = [&](juce::MidiBuffer midi)
        {
            rig.block(512, std::move(midi));
            rig.render(20 * 512);                                     // let the gate open and the pitch settle
            const auto w = rig.render(16 * 512);
            return crossingHz(w, 0, w.getNumSamples());
        };
        const double f60 = pitchAfter(midiOf({ on(60) }));
        const double f72 = pitchAfter(midiOf({ on(72) }));            // a newer, higher note wins
        const double f67 = pitchAfter(midiOf({ on(67) }));            // newest again
        const double fBack = pitchAfter(midiOf({ off(67) }));         // releasing the top note: 72 sounds
        const double fLow = pitchAfter(midiOf({ off(72) }));          // then 60
        logMessage("held-note pitch (Hz): 60 -> " + juce::String(f60, 2) + ", +72 -> " + juce::String(f72, 2) + ", +67 -> " + juce::String(f67, 2)
                   + ", -67 -> " + juce::String(fBack, 2) + ", -72 -> " + juce::String(fLow, 2));
        expectWithinAbsoluteError(f72 / f60, 2.0, 0.08);
        expectWithinAbsoluteError(f67 / f60, std::pow(2.0, 7.0 / 12.0), 0.08);
        expectWithinAbsoluteError(fBack / f60, 2.0, 0.08);
        expectWithinAbsoluteError(fLow / f60, 1.0, 0.04);

        const auto still = rig.render(8 * 512);
        expect(peakOf(still, 0, still.getNumSamples()) > 0.01f, "a note is still held (60), so the drone sounds");

        // Note-offs of notes that were never pressed change nothing; a duplicate note-on moves it to the top.
        rig.block(512, midiOf({ off(33), on(60) }));
        rig.render(2 * 512);
        rig.block(512, midiOf({ off(60) }));
        expectEquals(peakOf(rig.render(4 * 512), 0, 4 * 512), 0.0f, "one note-off releases a note pressed twice (the stack de-duplicates)");
    }

    // ── The retire fade of a count-reducing swap ───────────────────────────────────────────────
    // A voice dropped by a state restore while the transport plays keeps rendering its OLD config under a
    // falling gain for 25 ms (1200 samples at 48 kHz). All of it is read off a soloed DC voice, so the
    // output IS the gain.
    struct RetireRig
    {
        RetireRig(int voices, int soloVoice) : rig(512)
        {
            for (int v = 1; v < voices; ++v) rig.p.addLayer();
            for (int v = 0; v < voices; ++v) rig.dcVoice(v, 0.0f);
            rig.isolate(soloVoice, voices);
            rig.render(3 * 512);
        }
        Rig rig;
    };

    void retirePins()
    {
        beginTest("Retire: a restore with fewer voices while playing fades the dropped voice over exactly 1200 samples, linearly; while stopped, or by removeLayer, it is cut");
        {
            RetireRig r(3, 2);
            auto& rig = r.rig;
            const auto live = rig.block(64);
            const float level = live.getSample(0, 63);
            expect(level > 0.01f, "the soloed voice sounds");

            const auto shrunk = rig.blob(2);
            rig.restore(shrunk);
            Buf tail(2, 1500);
            for (int at = 0; at < 1500; at += 100)
            {
                const auto b = rig.block(100);
                for (int c = 0; c < 2; ++c) tail.copyFrom(c, at, b, c, 0, 100);
            }
            logMessage("retire tail: first " + juce::String(tail.getSample(0, 0) / level, 4) + ", at 600 " + juce::String(tail.getSample(0, 600) / level, 4)
                       + ", at 1199 " + juce::String(tail.getSample(0, 1199) / level, 6) + ", last non-zero index " + juce::String(lastNonZero(tail)));
            expectWithinAbsoluteError(tail.getSample(0, 0) / level, 1.0f, 0.03f);
            expectWithinAbsoluteError(tail.getSample(0, 600) / level, 0.5f, 0.03f);
            expectEquals(lastNonZero(tail), kRetireSamples - 1, "the tail ends at exactly sample 1199 of the fade");
            expect(tail.getSample(0, kRetireSamples - 1) > 0.0f, "the last sample of the fade is still above zero");
        }

        {
            RetireRig r(3, 2);
            auto& rig = r.rig;
            rig.host.playing = false;
            rig.block(64);          // the block that tells the swapper the transport stopped
            rig.restore(rig.blob(2));
            const auto after = rig.block(256);
            expectEquals(peakOf(after, 0, 256), 0.0f, "stopped: the dropped voice is cut at once, no fade");
        }

        {
            RetireRig r(3, 2);
            auto& rig = r.rig;
            expect(rig.p.removeLayer(2));
            expectEquals(peakOf(rig.block(256), 0, 256), 0.0f, "removeLayer while playing: the voice is cut at once, no fade");
        }
    }

    // The latent defect: a tail armed by a swap is only counted down while the voice is dropped. Regrow the
    // voice inside the fade and the countdown freezes, still armed; the next swap that drops it skips arming
    // (a tail "still ringing") and so plays the OLD snapshot from the first swap for what is left of it.
    // These tests pin that as it is today; the retire bin replaces it and the goldens move then.
    void staleRetirePins()
    {
        // Renders the tail of voice 2 after the last restore in the sequence, in 64-sample blocks.
        struct Tail { Buf audio; float levelA = 0, levelB = 0; };

        auto run = [](int regrowAfterSamples, int gapAfterRegrow) -> Tail
        {
            RetireRig r(3, 2);
            auto& rig = r.rig;
            Tail t;
            t.levelA = rig.block(64).getSample(0, 63);                  // config A: level 0 dB

            // First drop (arms the tail with config A), then play `regrowAfterSamples` of it.
            rig.restore(rig.blob(2));
            for (int done = 0; done < regrowAfterSamples; done += 60) rig.block(60);

            // Regrow: the voice is live again; give it a different config B (level -30 dB).
            rig.restore(rig.blob(3));
            setVoice(rig.p.apvts, 2, "level", -30.0f);
            rig.render(gapAfterRegrow, 64);
            t.levelB = rig.block(64).getSample(0, 63);

            // Second drop, then the tail.
            rig.restore(rig.blob(2));
            t.audio.setSize(2, 1500);
            for (int at = 0; at < 1500; at += 100)
            {
                const auto b = rig.block(100);
                for (int c = 0; c < 2; ++c) t.audio.copyFrom(c, at, b, c, 0, 100);
            }
            return t;
        };

        beginTest("Shrink, regrow inside the fade, shrink again: the second tail is the OLD snapshot and only what was left of the first countdown");
        {
            const auto stale = run(240, 2000);
            logMessage("stale tail: config A level " + juce::String(stale.levelA, 5) + ", config B level " + juce::String(stale.levelB, 6)
                       + ", tail first sample " + juce::String(stale.audio.getSample(0, 0), 5) + ", last non-zero index " + juce::String(lastNonZero(stale.audio)));
            expect(stale.levelB < 0.1f * stale.levelA, "config B is much quieter than config A");
            // 240 samples of the first fade were played before the regrow, so 960 were left on the countdown.
            expectEquals(lastNonZero(stale.audio), kRetireSamples - 240 - 1, "the second tail ends after the remaining 960 samples, not a fresh 1200");
            // The engine smooths a level change, so the tail climbs from B's level towards A's while it falls; judge by its peak.
            const float stalePeak = peakOf(stale.audio, 0, kRetireSamples);
            logMessage("stale tail peak " + juce::String(stalePeak, 5));
            expect(stalePeak > 4.0f * stale.levelB, "and it plays config A (louder), not the current config B");
        }

        beginTest("Shrink, let the fade finish, regrow, shrink again: the second tail is a fresh 1200 samples of the current config (the case that works)");
        {
            const auto fresh = run(1500, 2000);
            logMessage("fresh tail: config A level " + juce::String(fresh.levelA, 5) + ", config B level " + juce::String(fresh.levelB, 6)
                       + ", tail first sample " + juce::String(fresh.audio.getSample(0, 0), 6) + ", last non-zero index " + juce::String(lastNonZero(fresh.audio)));
            expectEquals(lastNonZero(fresh.audio), kRetireSamples - 1, "a full 1200-sample fade");
            expect(peakOf(fresh.audio, 0, kRetireSamples) < 1.05f * fresh.levelB, "and it plays the current config B: no louder than B's level");
        }

        beginTest("Back to back: 4 -> 2 drops voices 2 and 3, 2 -> 3 regrows voice 2, 3 -> 2 drops voice 2 again; voice 3 is never re-armed");
        {
            // Voice 2 is read as above; voice 3 (never regrown) in a second run of the same sequence.
            auto sequence = [](int soloVoice) -> Tail
            {
                RetireRig base(4, soloVoice);
                auto& rig = base.rig;
                Tail t;
                t.levelA = rig.block(64).getSample(0, 63);
                rig.restore(rig.blob(2));          // drops voices 2 and 3: both armed with 1200
                rig.block(120);                    // 120 consumed by each
                rig.restore(rig.blob(3));          // voice 2 live again (its countdown freezes); voice 3 still fading
                rig.block(120);                    // voice 3 consumes another 120
                rig.restore(rig.blob(2));          // drops voice 2 again: arming skipped, the frozen countdown resumes
                t.audio.setSize(2, 1500);
                for (int at = 0; at < 1500; at += 100)
                {
                    const auto b = rig.block(100);
                    for (int c = 0; c < 2; ++c) t.audio.copyFrom(c, at, b, c, 0, 100);
                }
                return t;
            };
            const auto v2 = sequence(2), v3 = sequence(3);
            logMessage("back to back: voice 2 tail ends at " + juce::String(lastNonZero(v2.audio)) + ", voice 3 tail ends at " + juce::String(lastNonZero(v3.audio)));
            expectEquals(lastNonZero(v2.audio), kRetireSamples - 120 - 1, "voice 2: the second drop plays the 1080 samples the first arming had left when the voice regrew");
            expectEquals(lastNonZero(v3.audio), kRetireSamples - 240 - 1, "voice 3: one fade only, run down through both later restores and never re-armed");
        }
    }

    // ── P6: a structural edit mid-play ─────────────────────────────────────────────────────────
    void structuralEditPins()
    {
        beginTest("A block skipped by an edit in flight is silent, yet the Note-mode stack and gate still follow the MIDI of the skipped blocks");
        {
            Rig rig;
            rig.dcVoice(0);
            rig.p.setMidiNoteMode(1);
            rig.render(2 * 512);
            rig.block(512, midiOf({ on(60) }));
            rig.render(4 * 512);
            const float steady = rig.block(512).getSample(0, 511);
            expect(steady > 0.01f, "the held note sounds");

            // The note-off arrives in a skipped block: the block is silent, but the gate still closes, so
            // the first block rendered after the edit is already shut.
            rig.p.layerEditInFlight.store(true);
            const auto skipped = rig.block(512, midiOf({ off(60) }));
            const auto skipped2 = rig.block(512);
            rig.p.layerEditInFlight.store(false);
            expectEquals(peakOf(skipped, 0, 512), 0.0f, "the skipped block is silent");
            expectEquals(peakOf(skipped2, 0, 512), 0.0f, "so is the next one");
            expectEquals(peakOf(rig.block(512), 0, 512), 0.0f, "the release in a skipped block was not lost: the first block after the edit is shut");

            // A note-on in a skipped block likewise opens the gate (and sets the pitch track) before the
            // render comes back, so the first rendered block is already at full level from sample 0.
            rig.p.layerEditInFlight.store(true);
            rig.block(512, midiOf({ on(64) }));
            rig.block(512);
            rig.p.layerEditInFlight.store(false);
            const auto resumed = rig.block(512);
            logMessage("note-on during skipped blocks: first sample after " + juce::String(resumed.getSample(0, 0) / steady, 4) + " of steady");
            expectWithinAbsoluteError(resumed.getSample(0, 0) / steady, 1.0f, 0.02f);
        }

        beginTest("A note held across addLayer, swapLayers and removeLayer keeps the gate open (an edit does not release it); a moved voice restarts its filter");
        {
            Rig rig;
            rig.dcVoice(0);
            rig.p.setMidiNoteMode(1);
            rig.render(2 * 512);
            rig.block(512, midiOf({ on(60) }));
            rig.render(4 * 512);
            const float steady = rig.block(512).getSample(0, 511);

            rig.p.addLayer();
            expectWithinAbsoluteError(rig.block(512).getSample(0, 0) / steady, 1.0f, 0.02f);
            rig.p.swapLayers(0, 1);          // the sounding voice now sits in slot 1, a default voice in slot 0
            rig.block(512);
            rig.p.removeLayer(0);            // removing the default one moves the sounding voice back to slot 0
            const auto after = rig.block(512);
            logMessage("after add / swap / remove: first sample " + juce::String(after.getSample(0, 0) / steady, 4) + " of steady (the move reset the voice's filter, so it restarts)");
            const auto later = rig.render(20 * 512);
            expectWithinAbsoluteError(later.getSample(0, later.getNumSamples() - 1) / steady, 1.0f, 0.02f);
            rig.block(512, midiOf({ off(60) }));
            expectEquals(peakOf(rig.render(3 * 512), 0, 3 * 512), 0.0f, "the held-note stack survived the edits, so one note-off still releases it");
        }

        beginTest("The gate pattern follows the host beat across skipped blocks: nothing is replayed, and the skipped part is silent");
        {
            auto make = [](Rig& r)
            {
                r.dcVoice(0);
                setVoice(r.p.apvts, 0, "gate_bypass", 0.0f);
                auto& g = r.p.getPattern(0).gate;
                g.patternLengthBars.store(1);
                g.addEnvelope({ 8, 4, 0.9f, 0.0f, 0.0f, false });   // beats 2 to 3: a gate open for a whole beat
            };
            Rig plain, edited;
            make(plain); make(edited);
            const auto ref = plain.render(72000);                   // beats 0 to 3

            edited.render(48000);                                   // beats 0 to 2: the gate is still shut
            edited.p.layerEditInFlight.store(true);
            const auto skipped = edited.render(12000);              // beats 2 to 2.5, the first half of the open region
            edited.p.layerEditInFlight.store(false);
            const auto rest = edited.render(12000);                 // beats 2.5 to 3
            expectEquals(peakOf(skipped, 0, 12000), 0.0f, "the skipped half-beat is silent");
            const float want = rmsOf(ref, 60000, 72000), got = rmsOf(rest, 0, 12000);
            logMessage("gate region after the skip: rms " + juce::String(got, 5) + " vs " + juce::String(want, 5) + " without a skip");
            expectWithinAbsoluteError(got, want, 0.15f * want);
        }
    }

    // ── P7: a block larger than the prepared maximum, at the engine ─────────────────────────────
    void oversizeBlockPins()
    {
        beginTest("VoiceEngine::process renders at most the prepared block and drops the rest; the oscillators advance by what was rendered");
        {
            mu_wavetable::WavetableBank bank;
            bank.loadFactoryBank();
            mu_tant::VoiceConfig cfg;
            auto make = [&]
            {
                auto e = std::make_unique<mu_tant::VoiceEngine>();
                e->prepare(kSr, 256);
                e->setBank(&bank);
                e->setSlotIndex(0, 0);
                e->setConfig(cfg);
                return e;
            };

            auto over = make(), ref = make();
            Buf got(2, 512), want(2, 256), second(2, 256), wantSecond(2, 256);
            got.clear(); want.clear(); second.clear(); wantSecond.clear();

            over->process(got, 512);      // asked for twice the prepared size
            ref->process(want, 256);

            bool same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i) same = same && got.getSample(c, i) == want.getSample(c, i);
            expect(same, "the first 256 frames are exactly a prepared-size render");
            expect(peakOf(got, 0, 256) > 0.01f, "which sounds");
            expectEquals(peakOf(got, 256, 512), 0.0f, "frames 256 to 511 are left at silence, with no error");

            over->process(second, 256);
            ref->process(wantSecond, 256);
            same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i) same = same && second.getSample(c, i) == wantSecond.getSample(c, i);
            expect(same, "the engine advanced by 256 frames, not 512: the next block continues from frame 256");
        }

        // Not exercised, only recorded: PluginProcessor::processBlock hands the block to MixerEngine, whose
        // channel buffers prepare() sized at the prepared block, so an oversize host block runs off the end
        // of them (it crashes a Debug heap). The contract has to say what an oversize block does (clamp or
        // chunk) before a golden can include one; until then the golden schedules stay within the prepared size.
    }
};

static MuTantExtractionPinTests muTantExtractionPinTests;
