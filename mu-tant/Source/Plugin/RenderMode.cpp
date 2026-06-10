#include "RenderMode.h"
#include "Plugin/PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace mu_tant::render_mode {

namespace {

// Tiny CLI helper: extract the value following `--flag` from argv-style tokens.
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
    std::fputs(("mu-tant render: error: " + msg + "\n").toRawUTF8(), stderr);
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

    const juce::String out     = takeFlagValue(tokens, "--out");
    const juce::String seconds = takeFlagValue(tokens, "--seconds");
    const juce::String sr      = takeFlagValue(tokens, "--samplerate");
    const juce::String bs      = takeFlagValue(tokens, "--blocksize");

    if (out.isEmpty())
    {
        reportError("--out <output.wav> is required when --render is given");
        return a;
    }
    a.outputWav = juce::File::getCurrentWorkingDirectory().getChildFile(out);
    if (seconds.isNotEmpty()) a.seconds    = seconds.getDoubleValue();
    if (sr.isNotEmpty())      a.sampleRate = sr.getDoubleValue();
    if (bs.isNotEmpty())      a.blockSize  = bs.getIntValue();

    a.seconds    = juce::jlimit(0.05, 600.0, a.seconds);
    a.sampleRate = juce::jlimit(8000.0, 192000.0, a.sampleRate);
    a.blockSize  = juce::jlimit(16, 8192, a.blockSize);
    a.valid      = true;
    return a;
}

int execute(const Args& args)
{
    // Default processor state: one voice. Bypass the gater so the raw oscillator drone
    // passes through (gateModeFor: bypassed → Pass). Without this the gate is closed when
    // the transport is stopped AND when playing an empty pattern, so the render would be
    // silent — this gives guaranteed non-silent audio with no preset / sample dependency.
    PluginProcessor proc;

    proc.onLoadError = [](const juce::String& m)
    { std::fputs(("mu-tant render: load: " + m + "\n").toRawUTF8(), stderr); std::fflush(stderr); };

    if (auto* p = proc.apvts.getParameter("v0_gate_bypass"))
        p->setValueNotifyingHost(1.0f);

    proc.setPlayConfigDetails(0, 2, args.sampleRate, args.blockSize);
    proc.prepareToPlay(args.sampleRate, args.blockSize);

    const int totalSamples = (int) std::ceil(args.seconds * args.sampleRate);
    const int outChannels  = juce::jmax(proc.getTotalNumOutputChannels(), 2);

    juce::AudioBuffer<float> block((int) outChannels, args.blockSize);
    juce::AudioBuffer<float> captured((int) outChannels, totalSamples);
    captured.clear();
    juce::MidiBuffer midi;

    // Render the full duration block by block, copying each block into the capture buffer.
    int written = 0;
    while (written < totalSamples)
    {
        const int ns = juce::jmin(args.blockSize, totalSamples - written);
        if (ns != args.blockSize)
            block.setSize((int) outChannels, ns, false, false, true);
        block.clear();
        midi.clear();
        proc.processBlock(block, midi);

        for (int ch = 0; ch < outChannels; ++ch)
            captured.copyFrom(ch, written, block, ch, 0, ns);
        written += ns;
    }

    proc.releaseResources();

    // Write WAV (24-bit). Deleting the writer finalises the header.
    args.outputWav.getParentDirectory().createDirectory();
    if (args.outputWav.existsAsFile())
        args.outputWav.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> outStream(args.outputWav.createOutputStream());
    if (outStream == nullptr)
    {
        reportError("could not open output file for writing: " + args.outputWav.getFullPathName());
        return 3;
    }

    const auto writerOptions = juce::AudioFormatWriterOptions{}
                                   .withSampleRate (args.sampleRate)
                                   .withNumChannels (outChannels)
                                   .withBitsPerSample (24);
    auto writer = wav.createWriterFor (outStream, writerOptions);
    if (writer == nullptr)
    {
        reportError("could not create WAV writer");
        return 3;
    }
    if (! writer->writeFromAudioSampleBuffer(captured, 0, captured.getNumSamples()))
    {
        reportError("WAV write failed");
        return 3;
    }
    writer.reset();

    std::fprintf(stdout, "mu-tant render: wrote %d samples (%.3f s @ %.0f Hz, %d ch) -> %s\n",
                 totalSamples, args.seconds, args.sampleRate, outChannels,
                 args.outputWav.getFullPathName().toRawUTF8());
    std::fflush(stdout);
    return 0;
}

} // namespace mu_tant::render_mode
