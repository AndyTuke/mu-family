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
    const juce::String swapRhy    = takeFlagValue(tokens, "--swap-rhythm-preset");
    const juce::String swapRhySlot= takeFlagValue(tokens, "--swap-rhythm-slot");
    const juce::String swapRhyAt  = takeFlagValue(tokens, "--swap-rhythm-at");
    const juce::String midiProg   = takeFlagValue(tokens, "--midi-program");
    const juce::String midiProgPre= takeFlagValue(tokens, "--midi-program-preset");
    const juce::String midiProgAt = takeFlagValue(tokens, "--midi-program-at");

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
    if (swapRhy.isNotEmpty())
        a.swapRhythmFile = juce::File::getCurrentWorkingDirectory().getChildFile(swapRhy);
    if (midiProgPre.isNotEmpty())
        a.midiProgramPreset = juce::File::getCurrentWorkingDirectory().getChildFile(midiProgPre);
    if (seconds.isNotEmpty()) a.seconds    = seconds.getDoubleValue();
    if (sr.isNotEmpty())      a.sampleRate = sr.getDoubleValue();
    if (bs.isNotEmpty())      a.blockSize  = bs.getIntValue();
    if (swapAt.isNotEmpty())  a.swapAtSeconds = swapAt.getDoubleValue();
    if (swapRhySlot.isNotEmpty()) a.swapRhythmSlot      = swapRhySlot.getIntValue();
    if (swapRhyAt.isNotEmpty())   a.swapRhythmAtSeconds = swapRhyAt.getDoubleValue();
    if (midiProg.isNotEmpty())    a.midiProgram          = midiProg.getIntValue();
    if (midiProgAt.isNotEmpty())  a.midiProgramAtSeconds = midiProgAt.getDoubleValue();

    // Each optional action needs both a target and a time; if either is missing, disable it.
    if (a.swapPresetFile == juce::File{} || a.swapAtSeconds < 0.0)
        a.swapAtSeconds = -1.0;
    if (a.swapRhythmFile == juce::File{} || a.swapRhythmAtSeconds < 0.0)
        a.swapRhythmAtSeconds = -1.0;
    if (a.midiProgramPreset == juce::File{} || a.midiProgram < 0 || a.midiProgramAtSeconds < 0.0)
        a.midiProgramAtSeconds = -1.0;

    a.seconds    = juce::jlimit(0.05, 600.0, a.seconds);
    a.sampleRate = juce::jlimit(8000.0, 192000.0, a.sampleRate);
    a.blockSize  = juce::jlimit(16, 8192, a.blockSize);
    a.valid      = true;
    return a;
}

int execute(const Args& args)
{
    // Suppress the ctor's automatic `_default.muClid` / `_default.muRhythm`
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
        if (ext == ".murhythm")
            proc.applyRhythmPreset(args.presetFile, 0);
        else if (ext == ".muclid")
            proc.loadPreset(args.presetFile);
        else
        {
            reportError("unrecognised preset extension: " + ext + " (expected .muRhythm or .muClid)");
            return 2;
        }
    }

    // Surface load errors to stderr so headless renders are debuggable
    // (e.g. a missing sample in a preset).
    proc.onLoadError = [](const juce::String& m)
    { std::fputs(("mu-clid render: load: " + m + "\n").toRawUTF8(), stderr); std::fflush(stderr); };

    // A2: seed the channel-9 full-preset map so an injected program change maps
    // to a real preset file. setPresetPath auto-saves, but with no storage file
    // set the save() is a no-op, so this stays in-memory (no disk write).
    if (args.midiProgramAtSeconds >= 0.0)
    {
        proc.midiFullPresetMap.setEnabled(true);
        proc.midiFullPresetMap.setPresetPath(args.midiProgram, args.midiProgramPreset);
    }

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

    // A9: per-rhythm deferred hot-swap (stageRhythmPreset).
    const int swapRhythmAtSample = (args.swapRhythmAtSeconds >= 0.0)
        ? (int) std::round(args.swapRhythmAtSeconds * args.sampleRate)
        : -1;
    bool swapRhythmTriggered = false;

    // A2: MIDI program-change injection (channel 9 → full-preset map).
    const int midiProgramAtSample = (args.midiProgramAtSeconds >= 0.0)
        ? (int) std::round(args.midiProgramAtSeconds * args.sampleRate)
        : -1;
    bool midiProgramInjected = false;

    int written = 0;
    bool wasPending = false;
    while (written < totalSamples)
    {
        if (swapAtSample >= 0 && ! swapTriggered && written >= swapAtSample)
        {
            proc.loadPreset(args.swapPresetFile);  // stages; commits at next loop point
            swapTriggered = true;
            std::fprintf(stderr, "mu-clid render: swap to %s requested at %.3fs\n",
                         args.swapPresetFile.getFileName().toRawUTF8(), written / args.sampleRate);
            std::fflush(stderr);
            wasPending = true;
        }

        // A9: stage a per-rhythm preset on one slot (defers to that rhythm's loop).
        if (swapRhythmAtSample >= 0 && ! swapRhythmTriggered && written >= swapRhythmAtSample)
        {
            proc.stageRhythmPreset(args.swapRhythmSlot, args.swapRhythmFile);
            swapRhythmTriggered = true;
            std::fprintf(stderr, "mu-clid render: rhythm-%d swap to %s staged at %.3fs\n",
                         args.swapRhythmSlot, args.swapRhythmFile.getFileName().toRawUTF8(),
                         written / args.sampleRate);
            std::fflush(stderr);
        }

        const int ns = juce::jmin(args.blockSize, totalSamples - written);
        block.clear();
        midi.clear();

        // A2: inject a channel-9 program change in this block (1-based MIDI
        // channel 9). scanMidiProgramChanges enqueues it; the async drain below
        // dispatches it through applyFullMidiPreset → loadPreset (deferred).
        if (midiProgramAtSample >= 0 && ! midiProgramInjected && written >= midiProgramAtSample)
        {
            midi.addEvent(juce::MidiMessage::programChange(9, args.midiProgram), 0);
            midiProgramInjected = true;
            wasPending = true;
            std::fprintf(stderr, "mu-clid render: MIDI program change %d (ch 9) injected at %.3fs\n",
                         args.midiProgram, written / args.sampleRate);
            std::fflush(stderr);
        }

        if (ns != args.blockSize)
            block.setSize((int) outChannels, ns, false, false, true);
        proc.processBlock(block, midi);

        // The render loop never yields to JUCE's message loop, so service any
        // triggerAsyncUpdate() (e.g. the staged swap commit) synchronously here.
        proc.flushPendingAsyncUpdates();

        if (wasPending && ! proc.hasPendingFullPreset())
        {
            std::fprintf(stderr, "mu-clid render: full-preset swap committed at %.3fs\n",
                         (written + ns) / args.sampleRate);
            std::fflush(stderr);
            wasPending = false;
        }

        for (int ch = 0; ch < outChannels; ++ch)
            captured.copyFrom(ch, written, block, ch, 0, ns);
        written += ns;
    }

    proc.releaseResources();

    // Write WAV: createWriterFor takes ownership of outStream on success (sets it to nullptr).
    // Deleting the writer finalises the WAV header.
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
    writer.reset();   // finalises the header on destruction

    std::fprintf(stdout, "mu-clid render: wrote %d samples (%.3f s @ %.0f Hz, %d ch) -> %s\n",
                 totalSamples, args.seconds, args.sampleRate, outChannels,
                 args.outputWav.getFullPathName().toRawUTF8());
    std::fflush(stdout);
    return 0;
}

} // namespace mu_clid::render_mode
