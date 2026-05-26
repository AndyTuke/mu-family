#include "RenderMode.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PresetIO.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_clid::render_mode {

namespace {

// Tiny CLI helper: extract the value following `--flag` from argv-style tokens.
// Returns empty StringRef when the flag is absent or has no value.
juce::String takeFlagValue(juce::StringArray& tokens, const juce::String& flag)
{
    const int idx = tokens.indexOf(flag);
    if (idx < 0 || idx + 1 >= tokens.size())
        return {};
    const juce::String value = tokens[idx + 1];
    tokens.remove(idx + 1);
    tokens.remove(idx);
    return value;
}

void reportError(const juce::String& msg)
{
    std::fputs(("mu-clid render: error: " + msg + "\n").toRawUTF8(), stderr);
    std::fflush(stderr);
}

} // namespace

Args parse(const juce::String& commandLine)
{
    Args a;
    auto tokens = juce::StringArray::fromTokens(commandLine, true);
    if (! tokens.contains("--render"))
        return a;
    tokens.removeString("--render");

    const juce::String out        = takeFlagValue(tokens, "--out");
    const juce::String preset     = takeFlagValue(tokens, "--preset");
    const juce::String seconds    = takeFlagValue(tokens, "--seconds");
    const juce::String sr         = takeFlagValue(tokens, "--samplerate");
    const juce::String bs         = takeFlagValue(tokens, "--blocksize");
    const juce::String swapPreset = takeFlagValue(tokens, "--swap-preset");
    const juce::String swapAt     = takeFlagValue(tokens, "--swap-at");

    if (out.isEmpty())
    {
        reportError("--out <output.wav> is required when --render is given");
        return a;
    }
    a.outputWav = juce::File::getCurrentWorkingDirectory().getChildFile(out);
    if (preset.isNotEmpty())
        a.presetFile = juce::File::getCurrentWorkingDirectory().getChildFile(preset);
    if (swapPreset.isNotEmpty())
        a.swapPresetFile = juce::File::getCurrentWorkingDirectory().getChildFile(swapPreset);
    if (seconds.isNotEmpty()) a.seconds    = seconds.getDoubleValue();
    if (sr.isNotEmpty())      a.sampleRate = sr.getDoubleValue();
    if (bs.isNotEmpty())      a.blockSize  = bs.getIntValue();
    if (swapAt.isNotEmpty())  a.swapAtSeconds = swapAt.getDoubleValue();

    // A swap needs both a target preset and a time; if either is missing, disable it.
    if (a.swapPresetFile == juce::File{} || a.swapAtSeconds < 0.0)
        a.swapAtSeconds = -1.0;

    a.seconds    = juce::jlimit(0.05, 600.0, a.seconds);
    a.sampleRate = juce::jlimit(8000.0, 192000.0, a.sampleRate);
    a.blockSize  = juce::jlimit(16, 8192, a.blockSize);
    a.valid      = true;
    return a;
}

int execute(const Args& args)
{
    // Suppress the ctor's automatic `_default.muclid` / `_default.muRhyth`
    // load so tests start from a single fresh default rhythm. The user's
    // personal default would otherwise pre-populate multiple rhythm slots
    // and contaminate the render.
    PluginProcessor::skipAutoLoadDefault = true;
    PluginProcessor proc;
    PluginProcessor::skipAutoLoadDefault = false;

    // Load the requested preset over the fresh default rhythm.
    if (args.presetFile != juce::File{})
    {
        if (! args.presetFile.existsAsFile())
        {
            reportError("preset file not found: " + args.presetFile.getFullPathName());
            return 2;
        }
        const auto ext = args.presetFile.getFileExtension().toLowerCase();
        if (ext == ".murhyth")
            proc.applyRhythmPreset(args.presetFile, 0);
        else if (ext == ".muclid")
            proc.loadPreset(args.presetFile);
        else
        {
            reportError("unrecognised preset extension: " + ext + " (expected .muRhyth or .muclid)");
            return 2;
        }
    }

    // Surface load errors + swap-commit timing to stderr so headless renders are
    // debuggable (e.g. a missing sample, or whether a mid-render swap committed).
    proc.onLoadError = [](const juce::String& m)
    { std::fputs(("mu-clid render: load: " + m + "\n").toRawUTF8(), stderr); std::fflush(stderr); };
    proc.onPresetSwapCommitted = []
    { std::fputs("mu-clid render: full-preset swap committed\n", stderr); std::fflush(stderr); };

    proc.setPlayConfigDetails(0, 2, args.sampleRate, args.blockSize);
    proc.prepareToPlay(args.sampleRate, args.blockSize);

    // Start the sequencer's internal play head — the standalone has no host
    // transport, so the test render relies on `internalPlaying` driving step
    // advancement (same as pressing Play in the transport bar).
    if (! proc.isInternalPlaying())
        proc.toggleInternalPlay();

    const int totalSamples = (int) std::ceil(args.seconds * args.sampleRate);
    const int outChannels  = juce::jmax(proc.getTotalNumOutputChannels(), 2);

    juce::AudioBuffer<float> block((int) outChannels, args.blockSize);
    juce::MidiBuffer midi;

    juce::AudioBuffer<float> captured((int) outChannels, totalSamples);
    captured.clear();

    // Optional mid-render preset swap (--swap-preset / --swap-at). Loading a
    // preset while the sequencer is playing stages a deferred swap that commits
    // at the next master loop boundary — this is how the listening tests exercise
    // the prestaged / tail-out full-preset hot-swap path headlessly.
    const int swapAtSample = (args.swapAtSeconds >= 0.0)
        ? (int) std::round(args.swapAtSeconds * args.sampleRate)
        : -1;
    bool swapTriggered = false;

    int written = 0;
    while (written < totalSamples)
    {
        if (swapAtSample >= 0 && ! swapTriggered && written >= swapAtSample)
        {
            proc.loadPreset(args.swapPresetFile);  // stages; commits at next loop point
            swapTriggered = true;
            std::fprintf(stderr, "mu-clid render: swap to %s requested at %.3fs\n",
                         args.swapPresetFile.getFileName().toRawUTF8(), written / args.sampleRate);
            std::fflush(stderr);
        }

        const int ns = juce::jmin(args.blockSize, totalSamples - written);
        block.clear();
        midi.clear();
        if (ns != args.blockSize)
            block.setSize((int) outChannels, ns, false, false, true);
        proc.processBlock(block, midi);

        // The render loop never yields to JUCE's message loop, so service any
        // triggerAsyncUpdate() (e.g. the staged swap commit) synchronously here.
        proc.flushPendingAsyncUpdates();

        for (int ch = 0; ch < outChannels; ++ch)
            captured.copyFrom(ch, written, block, ch, 0, ns);
        written += ns;
    }

    proc.releaseResources();

    // Write WAV. JUCE's WavAudioFormat owns the FormatWriter ownership semantics:
    // create the writer from the output stream, then `writeFromAudioSampleBuffer`,
    // then delete the writer (which finalises the WAV header).
    args.outputWav.getParentDirectory().createDirectory();
    if (args.outputWav.existsAsFile())
        args.outputWav.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> outStream(args.outputWav.createOutputStream());
    if (outStream == nullptr)
    {
        reportError("could not open output file for writing: " + args.outputWav.getFullPathName());
        return 3;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(outStream.get(), args.sampleRate, (unsigned int) outChannels,
                            24, {}, 0));
    if (writer == nullptr)
    {
        reportError("could not create WAV writer");
        return 3;
    }
    outStream.release();   // ownership transferred to writer

    if (! writer->writeFromAudioSampleBuffer(captured, 0, captured.getNumSamples()))
    {
        reportError("WAV write failed");
        return 3;
    }
    writer.reset();   // finalises the header on destruction

    std::fprintf(stdout, "mu-clid render: wrote %d samples (%.3f s @ %.0f Hz, %d ch) -> %s\n",
                 totalSamples, args.seconds, args.sampleRate, outChannels,
                 args.outputWav.getFullPathName().toRawUTF8());
    std::fflush(stdout);
    return 0;
}

} // namespace mu_clid::render_mode
