#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <cmath>
#include <cstdio>
#include <memory>

// Shared headless render-to-WAV scaffold for product `--render` modes.
//
// Plugin-agnostic: drives any juce::AudioProcessor, so it stays in mu-core. Each
// product's RenderMode parses its own extra CLI flags (preset swaps, MIDI program
// injection, …) and supplies per-block hooks; the parts every sibling shares —
// the common --out/--seconds/--samplerate/--blocksize parse, the block-by-block
// render loop, and the 24-bit WAV write — live here so a new product's RenderMode
// is just the product-specific glue. Header-only because it's compiled into the
// Standalone target only (where --render lives), so it adds nothing to plugin builds.
namespace mu_core::render_mode
{
    // The --render args every product shares.
    struct BaseArgs
    {
        juce::File outputWav;
        double     seconds    = 4.0;
        double     sampleRate = 48000.0;
        int        blockSize  = 512;
        bool       valid      = false;
    };

    // Extract the value following `--flag`, removing both tokens. Empty if absent.
    inline juce::String takeFlagValue (juce::StringArray& tokens, const juce::String& flag)
    {
        const int idx = tokens.indexOf (flag);
        if (idx < 0 || idx + 1 >= tokens.size())
            return {};
        const juce::String value = tokens[idx + 1];
        tokens.remove (idx + 1);
        tokens.remove (idx);
        return value;
    }

    inline void reportError (const char* product, const juce::String& msg)
    {
        std::fputs ((juce::String (product) + " render: error: " + msg + "\n").toRawUTF8(), stderr);
        std::fflush (stderr);
    }

    // Parse the common flags from `tokens` (caller has already confirmed + removed
    // `--render` and pulled any product-specific flags). Returns true and fills `a`
    // (valid = true) on success; false + reports to stderr when `--out` is missing.
    inline bool parseCommon (juce::StringArray& tokens, BaseArgs& a, const char* product)
    {
        const juce::String out     = takeFlagValue (tokens, "--out");
        const juce::String seconds = takeFlagValue (tokens, "--seconds");
        const juce::String sr      = takeFlagValue (tokens, "--samplerate");
        const juce::String bs      = takeFlagValue (tokens, "--blocksize");

        if (out.isEmpty())
        {
            reportError (product, "--out <output.wav> is required when --render is given");
            return false;
        }
        a.outputWav = juce::File::getCurrentWorkingDirectory().getChildFile (out);
        if (seconds.isNotEmpty()) a.seconds    = seconds.getDoubleValue();
        if (sr.isNotEmpty())      a.sampleRate = sr.getDoubleValue();
        if (bs.isNotEmpty())      a.blockSize  = bs.getIntValue();

        a.seconds    = juce::jlimit (0.05, 600.0, a.seconds);
        a.sampleRate = juce::jlimit (8000.0, 192000.0, a.sampleRate);
        a.blockSize  = juce::jlimit (16, 8192, a.blockSize);
        a.valid      = true;
        return true;
    }

    // Drive proc.processBlock over the full duration into `captured` (caller sizes it
    // [outChannels x totalSamples] and clears it). `beforeBlock(written, midi)` runs
    // after the block + midi buffers are cleared and before processBlock — inject MIDI
    // or trigger swaps there. `afterBlock(written, ns)` runs after processBlock — service
    // async updates / log there. Either hook may be empty.
    inline void renderLoop (juce::AudioProcessor& proc, const BaseArgs& a,
                            juce::AudioBuffer<float>& captured, int outChannels,
                            const std::function<void(int, juce::MidiBuffer&)>& beforeBlock = {},
                            const std::function<void(int, int)>& afterBlock = {})
    {
        const int totalSamples = captured.getNumSamples();
        juce::AudioBuffer<float> block (outChannels, a.blockSize);
        juce::MidiBuffer midi;

        int written = 0;
        while (written < totalSamples)
        {
            const int ns = juce::jmin (a.blockSize, totalSamples - written);
            if (ns != a.blockSize)
                block.setSize (outChannels, ns, false, false, true);
            block.clear();
            midi.clear();
            if (beforeBlock) beforeBlock (written, midi);
            proc.processBlock (block, midi);
            if (afterBlock) afterBlock (written, ns);
            for (int ch = 0; ch < outChannels; ++ch)
                captured.copyFrom (ch, written, block, ch, 0, ns);
            written += ns;
        }
    }

    // Write `captured` to a 24-bit WAV at a.outputWav. Returns 0 on success, 3 on error.
    inline int writeWav (const BaseArgs& a, const juce::AudioBuffer<float>& captured,
                         int outChannels, const char* product)
    {
        a.outputWav.getParentDirectory().createDirectory();
        if (a.outputWav.existsAsFile())
            a.outputWav.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> outStream (a.outputWav.createOutputStream());
        if (outStream == nullptr)
        {
            reportError (product, "could not open output file for writing: " + a.outputWav.getFullPathName());
            return 3;
        }

        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate (a.sampleRate)
                              .withNumChannels (outChannels)
                              .withBitsPerSample (24);
        auto writer = wav.createWriterFor (outStream, opts);
        if (writer == nullptr)
        {
            reportError (product, "could not create WAV writer");
            return 3;
        }
        if (! writer->writeFromAudioSampleBuffer (captured, 0, captured.getNumSamples()))
        {
            reportError (product, "WAV write failed");
            return 3;
        }
        writer.reset();   // finalises the WAV header on destruction

        std::fprintf (stdout, "%s render: wrote %d samples (%.3f s @ %.0f Hz, %d ch) -> %s\n",
                      product, captured.getNumSamples(), a.seconds, a.sampleRate, outChannels,
                      a.outputWav.getFullPathName().toRawUTF8());
        std::fflush (stdout);
        return 0;
    }
}
