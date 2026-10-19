// The Rhythm layer with no processor at all (extraction-plan 4.1, the self-containment proof): this target is
// compiled from mu-core's layer-side sources, the mu-clid-layer library and this file, with no Plugin/ source.
// It builds a SampleRhythm, gives it a voice engine and a sample, hands it a hit and renders it with a hand-built
// RenderContext; it also checks the MIDI output mode and the staged-preparation path. A layer that secretly needed
// the processor would fail to link here.

#include "Sequencer/SampleRhythm.h"
#include "Persistence/RhythmPresetTree.h"

namespace
{
    // A short 1 kHz sine as a mono 16-bit WAV in a temp file (the voice engine loads samples from files).
    juce::File writeTestSample()
    {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mu-clid-layer-test-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (stream != nullptr)
        {
            auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(44100.0)
                                                                                       .withNumChannels(1)
                                                                                       .withBitsPerSample(16));
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> b(1, 4410);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample(0, i, 0.5f * std::sin(juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 44100.0f));
                writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            }
        }
        return file;
    }

    mu_core::RenderContext blockContext(int blockSize)
    {
        auto ctx = mu_core::makeRenderContext();
        ctx.sampleRate = 44100.0;
        ctx.blockSize = blockSize;
        ctx.preparedBlockSize = blockSize;
        ctx.bpm = 120.0;
        ctx.beatsPerSample = (120.0 / 60.0) / 44100.0;
        ctx.callBeats = ctx.beatsPerSample * (double) blockSize;
        ctx.playing = 1;
        return ctx;
    }
}

struct MuClidLayerLibraryTests : juce::UnitTest
{
    MuClidLayerLibraryTests() : juce::UnitTest("mu-Clid Rhythm layer without a processor", "mu-clid-layer") {}

    void runTest() override
    {
        constexpr int kBlock = 512;
        const auto sample = writeTestSample();

        beginTest("A hit on a SampleRhythm with a sample renders sound through the layer alone, and silence without a hit");
        {
            mu_clid::SampleRhythm layer;
            layer.voice = std::make_unique<VoiceEngine>();
            layer.prepare(44100.0, kBlock);
            layer.voice->loadFile(sample);
            expectEquals(layer.typeId(), (int) mu_core::LayerTypeId::Rhythm, "the layer reports the Rhythm type");

            juce::AudioBuffer<float> out(2, kBlock);
            out.clear();
            layer.render(blockContext(kBlock), out, kBlock);   // no hit taken yet
            expectEquals(out.getMagnitude(0, kBlock), 0.0f, "no hit: silent");

            juce::MidiBuffer midiOut;
            layer.takeHit(true, false, midiOut);
            expect(midiOut.isEmpty(), "a sample hit writes no MIDI");
            out.clear();
            layer.render(blockContext(kBlock), out, kBlock);
            expect(out.getMagnitude(0, kBlock) > 0.01f, "a hit: the layer sounds");
        }

        beginTest("In MIDI output mode a hit becomes one note-on and never reaches the voice");
        {
            mu_clid::SampleRhythm layer;
            layer.voice = std::make_unique<VoiceEngine>();
            layer.prepare(44100.0, kBlock);
            layer.voice->loadFile(sample);
            layer.outputMode.store((int) mu_core::OutputMode::Midi);
            layer.midiNote.store(40);

            juce::MidiBuffer midiOut;
            layer.takeHit(false, false, midiOut);
            int noteOns = 0;
            for (const auto m : midiOut)
                if (m.getMessage().isNoteOn() && m.getMessage().getNoteNumber() == 40) ++noteOns;
            expectEquals(noteOns, 1, "one note-on for the configured note");

            juce::AudioBuffer<float> out(2, kBlock);
            out.clear();
            layer.render(blockContext(kBlock), out, kBlock);
            expectEquals(out.getMagnitude(0, kBlock), 0.0f, "the voice stays silent");
        }

        beginTest("prepareStaged builds a rhythm and a primed voice from a bare node, with no processor");
        {
            mu_clid::RhythmPresetContext ctx;
            ctx.sampleRate = 44100.0;
            ctx.blockSize = kBlock;
            const auto prepared = mu_clid::SampleRhythm::prepareStaged(juce::ValueTree("Rhythm"), "", ctx, "library-test");
            expect(prepared.voice != nullptr, "a voice engine is built");
            expect(prepared.samplePath.isEmpty(), "a node with no sample loads none");
        }

        sample.deleteFile();
    }
};

static MuClidLayerLibraryTests muClidLayerLibraryTests;
