// Layer-extraction safety net for mu-On, part 2: pins. Each test describes what the unextracted tree
// does TODAY about something a later stage will move (per-engine sound, the global swing and accent,
// a Rumble reading another track's output, the bass-to-kick sidechain, the stop edge, a structural
// edit mid-play, a block larger than the prepared size). Written and passing on the unmodified tree;
// a stage that moves the behaviour must pass them unedited, and a stage that has to edit one has
// changed behaviour, which goes to Andy.
//
// Part 1, the bit-exact golden renders, is GoldenRenderTests.cpp.

#include "GoldenTestRig.h"
#include "Audio/GrooveVoices.h"
#include "Tests/GoldenRender.h"

namespace
{
    using Buf = juce::AudioBuffer<float>;
    using mu_on::EngineType;
    using mu_golden_rig::OwnedProcessor;
    using mu_golden_rig::setParam;
    using mu_golden_rig::setRow;
    using mu_golden_rig::useTrack;

    constexpr double kSr = 48000.0;

    // Largest absolute sample over both channels in [from, to).
    float peakOf(const Buf& b, int from, int to)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            p = juce::jmax(p, b.getMagnitude(c, from, juce::jmax(0, to - from)));
        return p;
    }

    // RMS over both channels in [from, to).
    float rmsOf(const Buf& b, int from, int to)
    {
        double sum = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = from; i < to; ++i) { const double v = b.getSample(c, i); sum += v * v; }
        return (float) std::sqrt(sum / (double) juce::jmax(1, (to - from) * b.getNumChannels()));
    }

    // Index of the first sample on channel 0 above `thr`, or -1.
    int firstAbove(const Buf& b, float thr = 1.0e-7f)
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs(b.getSample(0, i)) > thr) return i;
        return -1;
    }

    // Upward zero crossings per second of channel 0 over [from, to): the frequency of a clean sine.
    double crossingHz(const Buf& b, int from, int to)
    {
        int n = 0;
        for (int i = from + 1; i < to; ++i)
            if (b.getSample(0, i - 1) < 0.0f && b.getSample(0, i) >= 0.0f) ++n;
        return (double) n * kSr / (double) (to - from);
    }

    // Root mean square of the first difference over the RMS: high for bright material, low for a low tone.
    float brightness(const Buf& b, int from, int to)
    {
        double d = 0.0, s = 0.0;
        for (int i = from + 1; i < to; ++i)
        {
            const double v = b.getSample(0, i), dv = v - b.getSample(0, i - 1);
            s += v * v; d += dv * dv;
        }
        return (float) std::sqrt(d / juce::jmax(1.0e-30, s));
    }

    // A real PluginProcessor with a mock host, prepared, driven block by block on the test thread.
    struct Rig
    {
        explicit Rig(int maxBlock = 512, bool prepareNow = true) : maxBlockSize(maxBlock)
        {
            host.bpm = 120.0;
            p.setPlayHead(&host);
            p.setRateAndBufferSizeDetails(kSr, maxBlock);
            if (prepareNow) prepare();
        }
        ~Rig() { p.setPlayHead(nullptr); p.releaseResources(); }

        void prepare() { p.prepareToPlay(kSr, maxBlockSize); }

        // One host block of `n` frames (0 is allowed); returns what the processor wrote.
        Buf block(int n)
        {
            Buf work(2, juce::jmax(1, n));
            work.clear();
            Buf view(work.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
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

        double beatOf(int frames) const { return (double) frames * host.bpm / (60.0 * kSr); }

        OwnedProcessor p;
        mu_golden::MockHost host;
        int maxBlockSize;
        juce::int64 frame = 0;
    };

    // Rows with exactly the listed steps on.
    void onlySteps(mu_on::PluginProcessor& p, int track, std::initializer_list<int> steps, std::initializer_list<int> accents = {})
    {
        char on[17] = "0000000000000000", ac[17] = "0000000000000000";
        for (int s : steps) on[s] = '1';
        for (int s : accents) ac[s] = '1';
        setRow(p, track, on, ac);
    }

    juce::String eng(int t, EngineType e, const char* x) { return mu_on::engineParamId(t, e, x); }
}

struct MuOnExtractionPinTests : juce::UnitTest
{
    MuOnExtractionPinTests() : juce::UnitTest("mu-On extraction pins", "mu-on-pins") {}

    void runTest() override
    {
        engineSoundPins();
        swingAndAccentPins();
        rumbleSourcePins();
        sidechainPins();
        stopEdgePins();
        structuralEditPins();
        oversizeBlockPins();
    }

    // ── Per-engine sound: Bass, Snare, Rumble (Kick and Hat have EngineTests) ──────────────────────
    void engineSoundPins()
    {
        beginTest("Bass engine: a sine at its tune, held by sustain, silent after reset");
        {
            mu_on::BassEngine bass;
            bass.prepare(kSr, 512);
            bass.setParams(41.2f, 0, 0.0f, 600.0f, 0.2f, 0.0f, 100.0f, 2.0f, 200.0f, 1.0f, 0.0f);
            Buf out(2, (int) kSr);
            out.clear();
            bass.trigger(1.0f);
            for (int at = 0; at < out.getNumSamples(); at += 512)
            {
                Buf view(out.getArrayOfWritePointers(), 2, at, juce::jmin(512, out.getNumSamples() - at));
                bass.render(view, view.getNumSamples());
            }
            logMessage("bass sine: crossingHz " + juce::String(crossingHz(out, 24000, 48000), 2)
                       + " rms " + juce::String(rmsOf(out, 24000, 48000), 4) + " peak " + juce::String(peakOf(out, 24000, 48000), 4));
            expectWithinAbsoluteError(crossingHz(out, 24000, 48000), 41.2, 1.5);
            expect(rmsOf(out, 24000, 48000) > 0.3f, "a sustain of 1 holds the note at level");

            bass.reset();
            Buf after(2, 512);
            after.clear();
            bass.render(after, 512);
            expectEquals(peakOf(after, 0, 512), 0.0f, "reset silences the voice and its filter at once");
        }

        beginTest("Bass engine: waves differ in brightness; sustain 0 decays to silence; the sub adds an octave below");
        {
            auto renderWave = [](int wave, float sub, float sus, float decMs, int frames)
            {
                mu_on::BassEngine bass;
                bass.prepare(kSr, 512);
                bass.setParams(41.2f, wave, sub, 4000.0f, 0.0f, 0.0f, 100.0f, 2.0f, decMs, sus, 0.0f);
                Buf out(2, frames);
                out.clear();
                bass.trigger(1.0f);
                for (int at = 0; at < frames; at += 512)
                {
                    Buf view(out.getArrayOfWritePointers(), 2, at, juce::jmin(512, frames - at));
                    bass.render(view, view.getNumSamples());
                }
                return out;
            };
            const auto sine = renderWave(0, 0.0f, 1.0f, 200.0f, 24000);
            const auto saw  = renderWave(1, 0.0f, 1.0f, 200.0f, 24000);
            const auto sq   = renderWave(2, 0.0f, 1.0f, 200.0f, 24000);
            logMessage("bass brightness sine " + juce::String(brightness(sine, 4000, 24000), 5) + " saw "
                       + juce::String(brightness(saw, 4000, 24000), 5) + " square " + juce::String(brightness(sq, 4000, 24000), 5));
            expect(brightness(saw, 4000, 24000) > 3.0f * brightness(sine, 4000, 24000), "saw is brighter than sine");
            expect(brightness(sq, 4000, 24000) > 3.0f * brightness(sine, 4000, 24000), "square is brighter than sine");

            // The sub is a sine at half the pitch, equal in level at sub = 1: two equal sines add in power, so +3 dB.
            const auto subbed = renderWave(0, 1.0f, 1.0f, 200.0f, 48000);
            const auto plain  = renderWave(0, 0.0f, 1.0f, 200.0f, 48000);
            const float subRatio = rmsOf(subbed, 12000, 48000) / rmsOf(plain, 12000, 48000);
            logMessage("bass sub: rms ratio sub 1 / sub 0 = " + juce::String(subRatio, 4));
            expectWithinAbsoluteError(subRatio, 1.414f, 0.05f);

            const auto decaying = renderWave(0, 0.0f, 0.0f, 100.0f, 96000);
            logMessage("bass decay: rms early " + juce::String(rmsOf(decaying, 2000, 6000), 4) + " late " + juce::String(rmsOf(decaying, 84000, 96000), 6));
            expect(rmsOf(decaying, 2000, 6000) > 0.1f, "sounds after the attack");
            expect(rmsOf(decaying, 84000, 96000) < 1.0e-3f, "decays to silence with sustain 0");
        }

        beginTest("Snare engine: sounds, decays, its decay and tune shorten it, and it is lower than the Hat");
        {
            auto renderSample = [](mu_on::SampleChannel::Kind kind, float tune, float decMs)
            {
                mu_on::SampleChannel ch;
                ch.prepare(kSr, 512, kind);
                ch.setParams(tune, decMs);
                Buf out(2, 48000);
                out.clear();
                ch.trigger(1.0f);
                for (int at = 0; at < 48000; at += 512)
                {
                    Buf view(out.getArrayOfWritePointers(), 2, at, juce::jmin(512, 48000 - at));
                    ch.render(view, view.getNumSamples());
                }
                return out;
            };
            const auto snare = renderSample(mu_on::SampleChannel::Kind::Snare, 0.0f, 160.0f);
            const auto shortDec = renderSample(mu_on::SampleChannel::Kind::Snare, 0.0f, 30.0f);
            const auto high = renderSample(mu_on::SampleChannel::Kind::Snare, 12.0f, 160.0f);
            const auto hat = renderSample(mu_on::SampleChannel::Kind::HiHat, 0.0f, 160.0f);
            logMessage("snare rms 0-5ms " + juce::String(rmsOf(snare, 0, 240), 4) + " 100-200ms " + juce::String(rmsOf(snare, 4800, 9600), 4)
                       + " 600-700ms " + juce::String(rmsOf(snare, 28800, 33600), 6) + " | short 100-200ms " + juce::String(rmsOf(shortDec, 4800, 9600), 5)
                       + " | +12 st 100-200ms " + juce::String(rmsOf(high, 4800, 9600), 5)
                       + " | brightness snare " + juce::String(brightness(snare, 0, 4800), 4) + " hat " + juce::String(brightness(hat, 0, 4800), 4));
            expect(rmsOf(snare, 0, 2400) > 0.05f, "the snare sounds on its trigger block");
            expect(rmsOf(snare, 28800, 33600) < 1.0e-3f, "and has decayed by 600 ms");
            expect(rmsOf(shortDec, 4800, 9600) < 0.5f * rmsOf(snare, 4800, 9600), "a shorter decay leaves less at 100-200 ms");
            expect(rmsOf(high, 4800, 9600) < rmsOf(snare, 4800, 9600), "a higher tune plays the one-shot faster, so less is left");
            expect(brightness(snare, 0, 4800) < brightness(hat, 0, 4800), "the snare is darker than the hat");
        }

        beginTest("Rumble engine: dry plus three tempo-synced taps, nothing from silence, level follows the bar envelope");
        {
            auto rig = [](double bpm, float envLevel)
            {
                auto r = std::make_unique<mu_on::RumbleEngine>();
                r->prepare(kSr, 512);
                // Drive 0, taps 0.5 / 0.35 / 0.25, no reverb, filter wide open: the output is the input plus the taps.
                r->setParams(bpm, 0.0f, 0.5f, 0.35f, 0.25f, 0.5f, 0.0f, 18000.0f, 20000.0f, 0.0f);
                r->setBarEnvLevel(envLevel);
                return r;
            };
            auto run = [](mu_on::RumbleEngine& r, int frames, bool impulse)
            {
                Buf out(2, frames);
                out.clear();
                Buf in(2, 512), silence(2, 512);
                silence.clear();
                for (int at = 0; at < frames; at += 512)
                {
                    in.clear();
                    if (impulse && at == 0) { in.setSample(0, 0, 1.0f); in.setSample(1, 0, 1.0f); }
                    Buf view(out.getArrayOfWritePointers(), 2, at, juce::jmin(512, frames - at));
                    r.render(in, view, view.getNumSamples());
                }
                return out;
            };

            auto r120 = rig(120.0, 1.0f);
            const auto out = run(*r120, 24000, true);
            auto peakNear = [&](int centre) { return peakOf(out, centre - 3, centre + 4); };
            logMessage("rumble taps at 120 bpm: dry " + juce::String(peakNear(0), 4) + " 1/16 " + juce::String(peakNear(6000), 4)
                       + " 2/16 " + juce::String(peakNear(12000), 4) + " 3/16 " + juce::String(peakNear(18000), 4)
                       + " between " + juce::String(peakOf(out, 7000, 11000), 6));
            expect(peakNear(0) > 0.5f, "the dry impulse passes");
            expectWithinAbsoluteError(peakNear(6000) / peakNear(0), 0.5f, 0.05f);
            expectWithinAbsoluteError(peakNear(12000) / peakNear(0), 0.35f, 0.05f);
            expectWithinAbsoluteError(peakNear(18000) / peakNear(0), 0.25f, 0.05f);
            expect(peakOf(out, 7000, 11000) < 0.01f * peakNear(0), "silence between the taps");

            auto r60 = rig(60.0, 1.0f);
            const auto slow = run(*r60, 24000, true);
            expectWithinAbsoluteError(peakOf(slow, 12000 - 3, 12000 + 4) / peakOf(slow, 0, 4), 0.5f, 0.05f);   // 1/16 of a beat is 12000 samples at 60 bpm

            auto half = rig(120.0, 0.5f);
            const auto halved = run(*half, 512, true);
            expectWithinAbsoluteError(peakOf(halved, 0, 4) / peakNear(0), 0.5f, 0.05f);

            auto quiet = rig(120.0, 1.0f);
            const auto none = run(*quiet, 4096, false);
            expectEquals(peakOf(none, 0, 4096), 0.0f, "a silent feed with no tail budget gives nothing");
        }
    }

    // ── The global swing and accent ──────────────────────────────────────────────────────────────
    void swingAndAccentPins()
    {
        beginTest("Global swing delays the odd 16ths of every track, by swing x half a 16th; even steps stay put");
        {
            auto onset = [](float swing, int step, const char* solo)
            {
                Rig rig;
                useTrack(rig.p, 0, EngineType::Kick);
                useTrack(rig.p, 1, EngineType::Snare);
                onlySteps(rig.p, 0, { step });
                onlySteps(rig.p, 1, { step });
                setParam(rig.p.apvts, eng(0, EngineType::Kick, "drive"), 0.0f);
                setParam(rig.p.apvts, "seq_swing", swing);
                setParam(rig.p.apvts, solo, 1.0f);
                return firstAbove(rig.render(24000));
            };
            for (const char* solo : { "ch0_solo", "ch1_solo" })
            {
                const int base1 = onset(0.0f, 1, solo);
                const int half1 = onset(0.5f, 1, solo);
                const int max1  = onset(1.0f, 1, solo);
                const int base2 = onset(0.0f, 2, solo);
                const int half2 = onset(0.5f, 2, solo);
                logMessage(juce::String(solo) + " onsets: step1 swing 0/0.5/1 = " + juce::String(base1) + "/" + juce::String(half1) + "/" + juce::String(max1)
                           + "  step2 swing 0/0.5 = " + juce::String(base2) + "/" + juce::String(half2));
                // 120 bpm at 48 kHz: a 16th is 6000 samples, so swing 0.5 moves a hit by 0.5 x 0.5 x 6000 = 1500.
                expect(std::abs((half1 - base1) - 1500) <= 1, juce::String(solo) + ": swing 0.5 moves step 1 by 1500 samples");
                expect(std::abs((max1 - base1) - 2700) <= 1, juce::String(solo) + ": swing is limited to 0.9, 2700 samples");
                expectEquals(half2, base2, juce::String(solo) + ": an even step does not move");
                expect(base1 > 5990 && base1 < 6010, juce::String(solo) + ": step 1 sits a 16th in");
            }
        }

        beginTest("Global accent sets the velocity of accented steps against 0.72 for plain ones, on the Kick and the Snare");
        {
            auto ratio = [](float accent, EngineType engine)
            {
                Rig rig;
                useTrack(rig.p, 0, engine);
                onlySteps(rig.p, 0, { 0, 4 }, { 0 });   // steps 0 and 4 are frames 0 and 24000; only step 0 is accented
                setParam(rig.p.apvts, eng(0, EngineType::Kick, "drive"), 0.0f);
                setParam(rig.p.apvts, "seq_accent", accent);
                const auto out = rig.render(48000);
                return peakOf(out, 0, 3000) / peakOf(out, 24000, 27000);
            };
            for (const auto engine : { EngineType::Kick, EngineType::Snare })
                for (const float a : { 1.0f, 0.4f })
                {
                    const float r = ratio(a, engine);
                    logMessage(juce::String(mu_on::engineName(engine)) + " accent " + juce::String(a) + ": accented / plain = " + juce::String(r, 4)
                               + " (expected " + juce::String(a / 0.72f, 4) + ")");
                    expectWithinAbsoluteError(r, a / 0.72f, 0.01f * (a / 0.72f));
                }
        }
    }

    // ── A Rumble reading another track's output ─────────────────────────────────────────────────
    void rumbleSourcePins()
    {
        // A Kick on track 0 (silenced at the strip), a Rumble on track 1; returns the Rumble's own audio.
        auto rumbleOut = [](std::function<void(Rig&)> setup)
        {
            Rig rig;
            useTrack(rig.p, 1, EngineType::Rumble);
            onlySteps(rig.p, 0, { 0, 8 });
            setParam(rig.p.apvts, "ch0_mute", 1.0f);   // the Kick is not heard, the Rumble still reads it
            setup(rig);
            return rig.render(48000);
        };

        beginTest("A Rumble reads its source's render before the strip: muted or zero level, it still processes it");
        {
            const auto muted = rumbleOut([](Rig& r) { setParam(r.p.apvts, mu_on::layerParamId(1, "src"), 0.0f); });
            const auto zero  = rumbleOut([](Rig& r) { setParam(r.p.apvts, mu_on::layerParamId(1, "src"), 0.0f);
                                                      setParam(r.p.apvts, "ch0_mute", 0.0f); setParam(r.p.apvts, "ch0_lvl", 0.0f); });
            logMessage("rumble on a muted kick: rms " + juce::String(rmsOf(muted, 0, 48000), 5) + "; on a zero-level kick: " + juce::String(rmsOf(zero, 0, 48000), 5));
            expect(rmsOf(muted, 0, 48000) > 0.005f, "the Rumble sounds from a muted Kick");
            expect(rmsOf(zero, 0, 48000) > 0.005f, "and from a Kick at level 0");
        }

        beginTest("A Rumble whose source is missing, itself, or another Rumble processes silence");
        {
            const auto notLive = rumbleOut([](Rig& r) { setParam(r.p.apvts, mu_on::layerParamId(1, "src"), 5.0f); });
            const auto self    = rumbleOut([](Rig& r) { setParam(r.p.apvts, mu_on::layerParamId(1, "src"), 1.0f); });
            const auto other   = rumbleOut([](Rig& r)
            {
                useTrack(r.p, 2, EngineType::Rumble);
                setParam(r.p.apvts, mu_on::layerParamId(2, "src"), 1.0f);   // a Rumble on a Rumble
                setParam(r.p.apvts, mu_on::layerParamId(1, "src"), 0.0f);
                setParam(r.p.apvts, "ch1_mute", 1.0f);                       // only track 2 is heard
            });
            logMessage("rumble source missing " + juce::String(peakOf(notLive, 0, 48000), 6) + " self " + juce::String(peakOf(self, 0, 48000), 6)
                       + " on a rumble " + juce::String(peakOf(other, 0, 48000), 6));
            expectEquals(peakOf(notLive, 0, 48000), 0.0f, "source not a live track");
            expectEquals(peakOf(self, 0, 48000), 0.0f, "source is the Rumble itself");
            expectEquals(peakOf(other, 0, 48000), 0.0f, "source is another Rumble");
        }

        beginTest("A Rumble that sits before its source in the order still hears it (non-Rumble tracks render first)");
        {
            Rig rig;
            useTrack(rig.p, 1, EngineType::Kick);
            useTrack(rig.p, 0, EngineType::Rumble);
            onlySteps(rig.p, 1, { 0, 8 });
            setParam(rig.p.apvts, mu_on::layerParamId(0, "src"), 1.0f);
            setParam(rig.p.apvts, "ch1_mute", 1.0f);
            const auto out = rig.render(48000);
            logMessage("rumble on slot 0 reading a kick on slot 1: rms " + juce::String(rmsOf(out, 0, 48000), 5));
            expect(rmsOf(out, 0, 48000) > 0.005f, "heard");
        }

        beginTest("A Rumble's source follows its track and it keeps ringing (delay taps) after the source goes quiet");
        {
            Rig rig;
            useTrack(rig.p, 1, EngineType::Rumble);
            onlySteps(rig.p, 0, { 0 });
            setParam(rig.p.apvts, mu_on::layerParamId(1, "src"), 0.0f);
            setParam(rig.p.apvts, "ch0_mute", 1.0f);
            setParam(rig.p.apvts, eng(1, EngineType::Rumble, "revmix"), 0.0f);
            const auto out = rig.render(96000);
            logMessage("rumble ring: rms 0.1-0.5 s " + juce::String(rmsOf(out, 4800, 24000), 5) + " at 1.5-2 s " + juce::String(rmsOf(out, 72000, 96000), 6));
            expect(rmsOf(out, 4800, 24000) > 0.005f, "the taps and body sound after the first hit");
        }
    }

    // ── The bass-to-kick sidechain (the shared MixerEngine duck) ───────────────────────────────
    void sidechainPins()
    {
        // Kick on track 0 (inaudible at the strip, still the duck source), a held Bass on track 1.
        auto bassOut = [](std::function<void(Rig&)> setup)
        {
            Rig rig;
            useTrack(rig.p, 1, EngineType::Bass);
            onlySteps(rig.p, 0, { 8 });
            onlySteps(rig.p, 1, { 0 });
            setParam(rig.p.apvts, "ch0_lvl", 0.0f);
            setParam(rig.p.apvts, eng(1, EngineType::Bass, "sus"), 1.0f);
            setParam(rig.p.apvts, eng(1, EngineType::Bass, "sub"), 0.0f);
            setParam(rig.p.apvts, eng(1, EngineType::Bass, "drive"), 0.0f);
            setParam(rig.p.apvts, eng(0, EngineType::Kick, "adec"), 40.0f);   // a short kick, so the duck can be seen to release
            setParam(rig.p.apvts, "ch1_scSrc", 1.0f);
            setParam(rig.p.apvts, "ch1_scAmt", 1.0f);
            setParam(rig.p.apvts, "ch1_scAtk", 1.0f);
            setParam(rig.p.apvts, "ch1_scRel", 200.0f);
            setup(rig);
            auto out = rig.render(48000 + 48000);
            return std::make_pair(out, rig.p.mixerEngine.sidechainGR[1].load());
        };

        beginTest("The Bass ducks while the Kick (channel 0) sounds: a full-amount duck takes it near silence, then it recovers");
        {
            const auto [ducked, grMeter] = bassOut([](Rig&) {});
            const auto [free, grFree] = bassOut([](Rig& r) { setParam(r.p.apvts, "ch1_scAmt", 0.0f); });
            // The kick lands at beat 2 = frame 48000 (step 8); the bass sounded from frame 0.
            const float steady = peakOf(free, 40000, 47000);
            const float during = peakOf(ducked, 48500, 50000);
            const float later  = peakOf(ducked, 48000 + 48000 - 2000, 48000 + 48000);
            logMessage("sidechain: free-bass steady " + juce::String(steady, 4) + " | ducked: before " + juce::String(peakOf(ducked, 40000, 47000), 4)
                       + " during " + juce::String(during, 5) + " late " + juce::String(later, 4) + " | GR meter ducked " + juce::String(grMeter, 4) + " free " + juce::String(grFree, 4));
            expect(steady > 0.1f, "the held bass sounds");
            expectWithinAbsoluteError(peakOf(ducked, 40000, 47000), steady, 0.01f * steady);   // not ducked before the kick
            expect(during < 0.1f * steady, "ducked to under a tenth while the kick sounds");
            expect(later > 0.9f * steady, "and back to near full a second after the kick has died away");
            expectEquals(grFree, 0.0f, "no reduction reported with the amount at 0");
            expect(grMeter >= 0.0f, "the GR meter is written");
        }

        beginTest("The duck needs an audible source: soloing the Bass, or muting the Kick, switches it off");
        {
            const auto [free, g0] = bassOut([](Rig& r) { setParam(r.p.apvts, "ch1_scAmt", 0.0f); });
            const auto [soloed, g1] = bassOut([](Rig& r) { setParam(r.p.apvts, "ch1_solo", 1.0f); });
            const auto [muted, g2] = bassOut([](Rig& r) { setParam(r.p.apvts, "ch0_lvl", 1.0f); setParam(r.p.apvts, "ch0_mute", 1.0f); });
            const float steady = peakOf(free, 48500, 50000);
            logMessage("duck with bass soloed " + juce::String(peakOf(soloed, 48500, 50000), 4) + " kick muted " + juce::String(peakOf(muted, 48500, 50000), 4)
                       + " (steady " + juce::String(steady, 4) + ")");
            expectWithinAbsoluteError(peakOf(soloed, 48500, 50000), steady, 0.01f * steady);
            expectWithinAbsoluteError(peakOf(muted, 48500, 50000), steady, 0.01f * steady);
            juce::ignoreUnused(g0, g1, g2);
        }
    }

    // ── P5: the stop edge ──────────────────────────────────────────────────────────────────────
    void stopEdgePins()
    {
        // Track 0 a held Bass triggered on step 0 only; track 1 a Kick (rig for the Rumble and FX cases).
        auto heldBass = [](Rig& rig)
        {
            useTrack(rig.p, 0, EngineType::Bass);
            onlySteps(rig.p, 0, { 0 });
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sus"), 1.0f);
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sub"), 0.0f);
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "drive"), 0.0f);
        };

        beginTest("Stop edge: the block in which the host stops is silent from its first sample; the engines were reset before the render");
        {
            Rig rig;
            heldBass(rig);
            const auto playing = rig.render(4 * 512);
            expect(rmsOf(playing, 1024, 2048) > 0.1f, "the held bass sounds while playing");

            rig.host.playing = false;
            const auto stopBlock = rig.block(512);
            const auto after = rig.block(512);
            logMessage("stop block peak " + juce::String(peakOf(stopBlock, 0, 512), 6) + " next " + juce::String(peakOf(after, 0, 512), 6));
            expectEquals(peakOf(stopBlock, 0, 512), 0.0f, "the stop block is silent in full, not faded");
            expectEquals(peakOf(after, 0, 512), 0.0f, "and so is every block after");
        }

        beginTest("Stop edge: parameters applied in the stop block survive the reset; the sequencer position is not reset, voices are");
        {
            Rig rig;
            heldBass(rig);
            rig.render(4 * 512);
            rig.host.playing = false;
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "tune"), 82.4f);   // set in the block that sees the stop
            rig.block(512);
            rig.host.playing = true;                                           // restart where it stopped: mid step 0
            const auto resumed = rig.render(4 * 512);
            logMessage("restart in place: rms " + juce::String(rmsOf(resumed, 0, 2048), 6));
            expectEquals(peakOf(resumed, 0, 2048), 0.0f, "no step lands, so the reset bass stays silent (the note was killed, not paused)");

            // The sequencer resyncs only when the position jumps more than one 16th from where it expects
            // to be, so a locate back to the start from inside the first 16th does not refire step 0.
            rig.host.ppq = 0.0;
            const auto nearStart = rig.render(2 * 512);
            expectEquals(peakOf(nearStart, 0, 1024), 0.0f, "a locate to beat 0 from within the first 16th does not retrigger step 0");

            rig.host.ppq = 8.0;                                                // a real jump: resync, step 0 of bar 3 fires
            const auto relocated = rig.render(24000);
            logMessage("after locate to beat 8: crossingHz " + juce::String(crossingHz(relocated, 6000, 24000), 2));
            expect(rmsOf(relocated, 6000, 24000) > 0.1f, "step 0 retriggers the bass after a real jump");
            expectWithinAbsoluteError(crossingHz(relocated, 6000, 24000), 82.4, 2.0);   // at the tune set during the stop block
        }

        beginTest("Stop edge: the reset covers the engines only; an FX-return tail keeps ringing, a Rumble's delays and reverb are cleared");
        {
            Rig fx;
            useTrack(fx.p, 0, EngineType::Kick);
            onlySteps(fx.p, 0, { 0 });
            setParam(fx.p.apvts, "ch0_sendRev", 1.0f);
            setParam(fx.p.apvts, "ret_rev_lvl", 1.0f);
            fx.render(3 * 512);
            fx.host.playing = false;
            const auto tail = fx.render(4 * 512);
            logMessage("reverb tail after stop: rms " + juce::String(rmsOf(tail, 0, 2048), 6));
            expect(rmsOf(tail, 0, 2048) > 1.0e-5f, "the reverb return rings on past the stop");

            Rig rumble;
            useTrack(rumble.p, 1, EngineType::Rumble);
            onlySteps(rumble.p, 0, { 0 });
            setParam(rumble.p.apvts, mu_on::layerParamId(1, "src"), 0.0f);
            setParam(rumble.p.apvts, "ch0_mute", 1.0f);
            const auto playing = rumble.render(8 * 512);
            expect(rmsOf(playing, 0, 4096) > 0.005f, "the Rumble sounds");
            rumble.host.playing = false;
            const auto stopped = rumble.render(4 * 512);
            expectEquals(peakOf(stopped, 0, 2048), 0.0f, "the Rumble's taps and reverb are cut at the stop");
        }

        beginTest("Stop edge: a 0-frame block that sees the stop does the reset too (and the next playing block has no edge)");
        {
            Rig rig;
            heldBass(rig);
            rig.render(4 * 512);
            rig.host.playing = false;
            const auto empty = rig.block(0);
            expectEquals(empty.getNumSamples(), 0);
            rig.host.playing = true;
            const auto resumed = rig.render(4 * 512);
            logMessage("after a 0-frame stop block, playing again: peak " + juce::String(peakOf(resumed, 0, 2048), 6));
            expectEquals(peakOf(resumed, 0, 2048), 0.0f, "the bass was reset by the 0-frame stop block (it is still silent)");
        }

        beginTest("Stop edge: a 0-frame block while playing changes nothing; stop then start without a block in between is no edge");
        {
            Rig rig;
            heldBass(rig);
            rig.render(4 * 512);
            rig.block(0);
            const auto still = rig.render(2 * 512);
            expect(rmsOf(still, 0, 1024) > 0.1f, "the held bass is still sounding after a 0-frame playing block");

            rig.host.playing = false;
            rig.host.playing = true;   // the processor never saw the stop
            const auto go = rig.render(2 * 512);
            expect(rmsOf(go, 0, 1024) > 0.1f, "no block saw a stop, so nothing was reset");
        }
    }

    // ── P6: a structural edit mid-play ─────────────────────────────────────────────────────────
    void structuralEditPins()
    {
        beginTest("Blocks skipped during an edit are silent, lose their step triggers, and leave the held note untouched (no release)");
        {
            Rig rig;
            useTrack(rig.p, 0, EngineType::Bass);
            useTrack(rig.p, 1, EngineType::Kick);
            onlySteps(rig.p, 0, { 0 });
            onlySteps(rig.p, 1, { 4, 8, 12 });   // the kicks fall inside the skipped window below
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sus"), 1.0f);
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sub"), 0.0f);
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "drive"), 0.0f);
            setParam(rig.p.apvts, "ch1_lvl", 0.0f);

            const auto before = rig.render(4 * 512);
            const int kicksBefore = rig.p.triggerCount(1);
            const float lastBefore = before.getSample(0, before.getNumSamples() - 1);

            rig.p.layerEditInFlight.store(true);   // what a running structural edit raises
            const auto skipped = rig.render(24000);   // 0.5 s: beats 0.17 - 0.67, kick steps 4 and 8 would land in it
            rig.p.layerEditInFlight.store(false);
            expectEquals(peakOf(skipped, 0, 24000), 0.0f, "skipped blocks are silent");
            expectEquals(rig.p.triggerCount(1), kicksBefore, "no step fired inside the skipped window");

            const auto resumed = rig.block(512);
            logMessage("held note across the skip: last sample before " + juce::String(lastBefore, 5) + " first after " + juce::String(resumed.getSample(0, 0), 5)
                       + " rms after " + juce::String(rmsOf(resumed, 0, 512), 4));
            expect(rmsOf(resumed, 0, 512) > 0.1f, "the held bass is sounding in the first block after the skip");
            expect(std::abs(resumed.getSample(0, 0) - lastBefore) < 0.03f, "its waveform continues from where it left off");
            expectEquals(rig.p.triggerCount(1), kicksBefore, "the missed steps are not fired late");
            rig.render(4 * 512);
            expect(rig.p.triggerCount(1) >= kicksBefore, "steps resume normally");
        }

        beginTest("A stop that falls in skipped blocks is acted on at the first block that runs: the reset happens then");
        {
            Rig rig;
            useTrack(rig.p, 0, EngineType::Bass);
            onlySteps(rig.p, 0, { 0 });
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sus"), 1.0f);
            rig.render(4 * 512);
            rig.p.layerEditInFlight.store(true);
            rig.host.playing = false;
            rig.render(2 * 512);
            rig.p.layerEditInFlight.store(false);
            const auto first = rig.block(512);
            expectEquals(peakOf(first, 0, 512), 0.0f, "the first block after the edit sees the stop edge, resets, and is silent");
            rig.host.playing = true;
            const auto next = rig.render(4 * 512);
            expectEquals(peakOf(next, 0, 2048), 0.0f, "and the bass does not come back");
        }

        beginTest("swapLayers while playing resets the voices of both slots and the rows move with their tracks");
        {
            Rig rig;
            useTrack(rig.p, 0, EngineType::Bass);
            useTrack(rig.p, 1, EngineType::Hat);
            onlySteps(rig.p, 0, { 0 });
            onlySteps(rig.p, 1, { 8 });
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sus"), 1.0f);
            const auto playing = rig.render(4 * 512);
            expect(rmsOf(playing, 1024, 2048) > 0.05f, "the bass sounds");

            rig.p.swapLayers(0, 1);
            expect(rig.p.engineOf(0) == EngineType::Hat && rig.p.engineOf(1) == EngineType::Bass, "the engines moved with their tracks");
            expect(rig.p.pattern().isOn(0, 8) && rig.p.pattern().isOn(1, 0), "and so did the rows");
            const auto after = rig.render(4 * 512);
            logMessage("after swapLayers(0, 1): peak over 4 blocks " + juce::String(peakOf(after, 0, 2048), 6));
            expectEquals(peakOf(after, 0, 2048), 0.0f, "the moved bass was reset by the edit and stays silent until its next step");
        }

        beginTest("addLayer / removeLayer while playing: the playing tracks carry on, the new slot starts from silence");
        {
            Rig rig;
            useTrack(rig.p, 0, EngineType::Bass);
            onlySteps(rig.p, 0, { 0 });
            setParam(rig.p.apvts, eng(0, EngineType::Bass, "sus"), 1.0f);
            rig.render(4 * 512);
            rig.p.addLayer();
            const auto grown = rig.render(2 * 512);
            expect(rmsOf(grown, 0, 1024) > 0.1f, "adding a track leaves the held bass sounding");
            rig.p.removeLayer(1);
            const auto shrunk = rig.render(2 * 512);
            expect(rmsOf(shrunk, 0, 1024) > 0.1f, "so does removing the one above it");
        }
    }

    // ── P7: a block larger than the prepared maximum ───────────────────────────────────────────
    void oversizeBlockPins()
    {
        beginTest("GrooveVoices::renderAll renders at most the prepared block and drops the rest; the engines advance by what was rendered");
        {
            mu_seam_test::TempStorage storage;
            mu_on::PluginProcessor p;
            auto make = [&p]
            {
                auto v = std::make_unique<mu_on::GrooveVoices>();
                v->prepare(kSr, 512);
                v->cacheParams(p.apvts);
                return v;
            };
            std::array<mu_on::Track*, mu_limits::kMaxLayers> tracks {};
            tracks[0] = &p.getTrack(0);
            auto fresh = [&](mu_on::GrooveVoices& v) { v.beginBlock(tracks.data(), 1); v.applyParams(0.0, 120.0); };

            auto over = make(), ref = make();
            Buf got(2, 1024), want(2, 512), second(2, 512);

            fresh(*over); over->trigger(0, 1.0f); over->renderAll(1024);
            over->copyOut(0, got, 1024);
            fresh(*ref); ref->trigger(0, 1.0f); ref->renderAll(512);
            ref->copyOut(0, want, 512);

            bool same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) same = same && got.getSample(c, i) == want.getSample(c, i);
            expect(same, "the first 512 frames are exactly a prepared-size render");
            expect(peakOf(got, 0, 512) > 0.1f, "which sounds");
            expectEquals(peakOf(got, 512, 1024), 0.0f, "frames 512 to 1023 are left at silence, with no error");

            fresh(*over); over->renderAll(512); over->copyOut(0, second, 512);
            fresh(*ref);  ref->renderAll(512);  ref->copyOut(0, want, 512);
            same = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i) same = same && second.getSample(c, i) == want.getSample(c, i);
            expect(same, "the engine advanced by 512 frames, not 1024: the next block continues from frame 512");
        }

        beginTest("A trigger whose offset lies beyond the rendered part of an oversize block is held and never sounds");
        {
            mu_seam_test::TempStorage storage;
            mu_on::PluginProcessor p;
            auto v = std::make_unique<mu_on::GrooveVoices>();
            v->prepare(kSr, 512);
            v->cacheParams(p.apvts);
            std::array<mu_on::Track*, mu_limits::kMaxLayers> tracks {};
            tracks[0] = &p.getTrack(0);
            Buf got(2, 512);

            v->beginBlock(tracks.data(), 1); v->applyParams(0.0, 120.0);
            v->trigger(0, 1.0f, 700);          // the step landed at frame 700 of a 1024-frame block
            v->renderAll(1024);                 // only 512 frames are rendered
            float peak = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                v->beginBlock(tracks.data(), 1); v->applyParams(0.0, 120.0);
                v->renderAll(512);
                v->copyOut(0, got, 512);
                peak = juce::jmax(peak, peakOf(got, 0, 512));
            }
            logMessage("held trigger at offset 700, eight later blocks: peak " + juce::String(peak, 6));
            expectEquals(peak, 0.0f, "the kick never starts: its onset offset is never reached in a 512-frame block");

            v->trigger(0, 1.0f, 0);
            v->renderAll(512);
            v->copyOut(0, got, 512);
            expect(peakOf(got, 0, 512) > 0.1f, "a fresh trigger replaces the held one and sounds");
        }

        // Not exercised, only recorded: MixerEngine::processBlock applies pan, gain, sidechain and the
        // peak readers to `numSamples` frames of channel buffers that prepare() sized at the prepared
        // block, so an oversize block through PluginProcessor::processBlock runs off the end of them (it
        // crashes a Debug heap). The contract has to say what an oversize block does (clamp or chunk)
        // before a golden can include one; until then the golden schedules stay within the prepared size.
    }
};

static MuOnExtractionPinTests muOnExtractionPinTests;
