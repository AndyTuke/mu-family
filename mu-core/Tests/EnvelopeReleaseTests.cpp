// Envelope parameter updates that must not cut a release, and must still land on the next note.
//
// juce::ADSR::setParameters recomputes the release rate as sustain / (release x sampleRate), so a voice that
// pushes its settings every block cut a Sustain-0 release at the next block. mu_env::applyParameters applies
// settings only when they changed and never while the envelope is releasing; VoiceEngine::trigger then applies
// whatever was held back. This file tests the helper directly and through the real VoiceEngine (mu-Clid's engine).
// (mu-Toni's voice has the same checks in its own ToniVoiceTests.cpp.)

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

#include "Audio/EnvelopeParams.h"
#include "Audio/VoiceEngine.h"

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 256;

    void advance(juce::ADSR& e, int samples)
    {
        for (int i = 0; i < samples; ++i) e.getNextSample();
    }

    // A 3 s steady 200 Hz sine at 0.8, written as a 16-bit WAV: long enough that the sample never ends under a test.
    juce::File writeTone(const juce::File& dir)
    {
        auto file = dir.getChildFile("env_tone.wav");
        file.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = file.createOutputStream();
            if (out == nullptr) return {};
            auto writer = wav.createWriterFor(out, juce::AudioFormatWriterOptions{}.withSampleRate(kSr)
                                                                                   .withNumChannels(1)
                                                                                   .withBitsPerSample(16));
            if (writer == nullptr) return {};
            juce::AudioBuffer<float> b(1, 3 * (int) kSr);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample(0, i, 0.8f * (float) std::sin(6.283185307179586 * 200.0 * (double) i / kSr));
            writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
        }   // the writer is destroyed here, which flushes the file
        return file;
    }
}

struct EnvelopeReleaseTests : juce::UnitTest
{
    EnvelopeReleaseTests() : juce::UnitTest("Envelope release: parameter updates never cut a tail", "mu-core") {}

    void runTest() override
    {
        applyParametersCases();
        voiceEngineCases();
    }

    // mu_env::applyParameters in each of its four situations, on a bare juce::ADSR.
    void applyParametersCases()
    {
        const juce::ADSR::Parameters a { 0.001f, 0.5f, 0.0f, 1.0f };   // Sustain 0: a release has no rate to recompute
        const juce::ADSR::Parameters b { 0.002f, 0.4f, 0.3f, 0.2f };
        auto makeEnv = [&]
        {
            juce::ADSR e;
            e.setSampleRate(kSr);
            e.setParameters(a);
            return e;
        };

        beginTest("applyParameters: applies when the envelope is idle, releasing flag or not");
        {
            for (const bool releasing : { false, true })
            {
                auto e = makeEnv();
                mu_env::applyParameters(e, b, releasing);
                expect(mu_env::sameParameters(e.getParameters(), b),
                       juce::String("idle envelope, releasing flag ") + (releasing ? "set" : "clear") + ": parameters not applied");
            }
        }

        beginTest("applyParameters: applies a changed setting to a running envelope that is not releasing");
        {
            auto e = makeEnv();
            e.noteOn();
            advance(e, 480);
            mu_env::applyParameters(e, b, false);
            expect(mu_env::sameParameters(e.getParameters(), b), "a running, unreleased envelope takes the new settings");
            expect(e.isActive(), "and keeps running");
        }

        beginTest("applyParameters: ignores a change while the envelope is releasing, and the tail keeps its length");
        {
            auto e = makeEnv();
            e.noteOn();
            advance(e, 4800);          // 100 ms into the 0.5 s decay: level about 0.8
            e.noteOff();               // release 1 s from there
            mu_env::applyParameters(e, b, true);
            expect(mu_env::sameParameters(e.getParameters(), a), "the held-back settings are not applied mid-release");
            advance(e, 24000);         // 0.5 s into the release
            expect(e.isActive(), "the tail is still running after 0.5 s");
            advance(e, 28800);         // 1.1 s in
            expect(! e.isActive(), "and has ended by 1.1 s");
        }

        beginTest("applyParameters: skips identical settings, so a Sustain-0 release is not cut");
        {
            auto e = makeEnv();
            e.noteOn();
            advance(e, 4800);
            e.noteOff();

            // Premise: calling setParameters during a Sustain-0 release really does end it (release rate 0 / ... = 0).
            auto control = e;
            control.setParameters(a);
            expect(! control.isActive(), "premise: juce::ADSR::setParameters cuts a Sustain-0 release");

            mu_env::applyParameters(e, a, false);   // unchanged settings, even with the flag clear
            expect(e.isActive(), "unchanged settings leave the release running");
            advance(e, 24000);
            expect(e.isActive(), "and it is still sounding 0.5 s later");
        }
    }

    // VoiceEngine with a real sample: the amp envelope shapes the output, so the tail is heard as level.
    void voiceEngineCases()
    {
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("mu-env-test-" + juce::String::toHexString(juce::Random().nextInt64()));
        dir.createDirectory();
        const auto tone = writeTone(dir);
        expect(tone.existsAsFile(), "the test tone was written");
        if (! tone.existsAsFile()) { dir.deleteRecursively(); return; }

        // Renders `blocks` blocks (pushing `push` before each when given) and returns the peak of the last one.
        auto render = [](VoiceEngine& ve, int blocks, const VoiceParams* push)
        {
            juce::AudioBuffer<float> out(2, kBlock);
            float lastPeak = 0.0f;
            for (int i = 0; i < blocks; ++i)
            {
                if (push != nullptr) ve.setParams(*push);
                out.clear();
                ve.process(out, kBlock);
                lastPeak = out.getMagnitude(0, 0, kBlock);
            }
            return lastPeak;
        };

        beginTest("VoiceEngine: Sustain-0 release lasts its full length when the settings are pushed every block");
        {
            VoiceEngine ve;
            ve.prepareToPlay(kSr, kBlock);
            ve.loadFile(tone);
            VoiceParams p;
            p.ampEnvAtk = 0.0f;  p.ampEnvDec = 0.5f;  p.ampEnvSus = 0.0f;  p.ampEnvRel = 1.0f;
            p.filterEnvSus = 0.0f;  p.filterEnvRel = 1.0f;
            p.pitchEnvSus = 0.0f;   p.pitchEnvRel = 1.0f;
            ve.setParams(p);
            ve.trigger();
            render(ve, 19, &p);              // about 100 ms into the decay (level about 0.8)

            ve.releaseHeldNote();            // the release is 1 s from the level the note had
            const float at500 = render(ve, 94, &p);   // 0.5 s into the release: level about 0.4 of the 0.8 tone
            const float at1200 = render(ve, 131, &p);  // 1.2 s in: the release ended at 1.0 s
            logMessage("peak 0.5 s into the release: " + juce::String(at500, 4) + ", 1.2 s in: " + juce::String(at1200, 6));
            expect(at500 > 0.05f, "the tail is gone after 0.5 s (peak " + juce::String(at500, 4) + "): a per-block parameter push cut it");
            expect(at1200 < 1.0e-4f, "the tail is still sounding at 1.2 s (peak " + juce::String(at1200, 6) + ")");
        }

        beginTest("VoiceEngine: a Release change made during a tail is in use when the next note starts");
        {
            VoiceEngine ve;
            ve.prepareToPlay(kSr, kBlock);
            ve.loadFile(tone);
            VoiceParams p1;
            p1.ampEnvAtk = 0.0f;  p1.ampEnvDec = 0.01f;  p1.ampEnvSus = 1.0f;  p1.ampEnvRel = 0.05f;   // a short tail
            ve.setParams(p1);
            ve.trigger();
            render(ve, 9, nullptr);          // about 48 ms, at sustain

            ve.releaseHeldNote();
            render(ve, 2, nullptr);          // 10 ms into the 50 ms release
            VoiceParams p2 = p1;
            p2.ampEnvRel = 0.8f;
            ve.setParams(p2);                // the change arrives mid-tail
            const float stillThere = render(ve, 1, nullptr);
            expect(stillThere > 0.01f, "the tail was not cut by the change (peak " + juce::String(stillThere, 4) + ")");
            render(ve, 20, nullptr);         // past the end of the old 50 ms tail, no further parameter push

            ve.trigger();                    // the held-back Release must apply here
            render(ve, 9, nullptr);
            ve.releaseHeldNote();
            const float at400 = render(ve, 75, nullptr);      // 0.4 s into the release
            const float at1000 = render(ve, 113, nullptr);    // 1.0 s in: the 0.8 s release has ended
            logMessage("peak 0.4 s into the second release: " + juce::String(at400, 4) + ", 1.0 s in: " + juce::String(at1000, 6));
            expect(at400 > 0.05f, "the new 0.8 s Release was not in use (peak " + juce::String(at400, 4) + " at 0.4 s)");
            expect(at1000 < 1.0e-4f, "the second tail ran past its 0.8 s Release (peak " + juce::String(at1000, 6) + ")");
        }

        dir.deleteRecursively();
    }
};

static EnvelopeReleaseTests envelopeReleaseTests;
