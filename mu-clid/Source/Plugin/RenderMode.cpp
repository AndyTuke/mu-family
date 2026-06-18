#include "RenderMode.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/PresetIO.h"

namespace mu_clid::render_mode {

namespace core = mu_core::render_mode;

Args parse(const juce::String& commandLine)
{
    Args a;
    auto tokens = juce::StringArray::fromTokens(commandLine, true);
    if (! tokens.contains("--render"))
        return a;
    tokens.removeString("--render");

    // Product-specific flags first, then the shared --out/--seconds/--samplerate/--blocksize.
    const juce::String preset     = core::takeFlagValue(tokens, "--preset");
    const juce::String swapPreset = core::takeFlagValue(tokens, "--swap-preset");
    const juce::String swapAt     = core::takeFlagValue(tokens, "--swap-at");
    const juce::String swapRhy    = core::takeFlagValue(tokens, "--swap-rhythm-preset");
    const juce::String swapRhySlot= core::takeFlagValue(tokens, "--swap-rhythm-slot");
    const juce::String swapRhyAt  = core::takeFlagValue(tokens, "--swap-rhythm-at");
    const juce::String midiProg   = core::takeFlagValue(tokens, "--midi-program");
    const juce::String midiProgPre= core::takeFlagValue(tokens, "--midi-program-preset");
    const juce::String midiProgAt = core::takeFlagValue(tokens, "--midi-program-at");

    if (! core::parseCommon(tokens, a, "mu-clid"))   // sets a.valid + clamps; reports if --out missing
        return a;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    if (preset.isNotEmpty())      a.presetFile        = cwd.getChildFile(preset);
    if (swapPreset.isNotEmpty())  a.swapPresetFile    = cwd.getChildFile(swapPreset);
    if (swapRhy.isNotEmpty())     a.swapRhythmFile    = cwd.getChildFile(swapRhy);
    if (midiProgPre.isNotEmpty()) a.midiProgramPreset = cwd.getChildFile(midiProgPre);
    if (swapAt.isNotEmpty())      a.swapAtSeconds       = swapAt.getDoubleValue();
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
            core::reportError("mu-clid", "preset file not found: " + args.presetFile.getFullPathName());
            return 2;
        }
        const auto ext = args.presetFile.getFileExtension().toLowerCase();
        if (ext == ".murhythm")
            proc.applyRhythmPreset(args.presetFile, 0);
        else if (ext == ".muclid")
            proc.loadPreset(args.presetFile);
        else
        {
            core::reportError("mu-clid", "unrecognised preset extension: " + ext + " (expected .muRhythm or .muClid)");
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

    juce::AudioBuffer<float> captured(outChannels, totalSamples);
    captured.clear();

    // Mid-render action trigger points (in samples); <0 = disabled.
    const int swapAtSample = (args.swapAtSeconds >= 0.0)
        ? (int) std::round(args.swapAtSeconds * args.sampleRate) : -1;
    const int swapRhythmAtSample = (args.swapRhythmAtSeconds >= 0.0)
        ? (int) std::round(args.swapRhythmAtSeconds * args.sampleRate) : -1;
    const int midiProgramAtSample = (args.midiProgramAtSeconds >= 0.0)
        ? (int) std::round(args.midiProgramAtSeconds * args.sampleRate) : -1;

    bool swapTriggered = false, swapRhythmTriggered = false, midiProgramInjected = false;
    bool wasPending = false;

    // beforeBlock: fire any due mid-render action. Full-preset / rhythm swaps stage a
    // deferred swap that commits at the next loop boundary; the MIDI PC is injected into
    // this block's buffer and dispatched by the async drain in afterBlock.
    auto beforeBlock = [&](int written, juce::MidiBuffer& midi)
    {
        if (swapAtSample >= 0 && ! swapTriggered && written >= swapAtSample)
        {
            proc.loadPreset(args.swapPresetFile);   // stages; commits at next loop point
            swapTriggered = true;
            std::fprintf(stderr, "mu-clid render: swap to %s requested at %.3fs\n",
                         args.swapPresetFile.getFileName().toRawUTF8(), written / args.sampleRate);
            std::fflush(stderr);
            wasPending = true;
        }
        if (swapRhythmAtSample >= 0 && ! swapRhythmTriggered && written >= swapRhythmAtSample)
        {
            proc.stageRhythmPreset(args.swapRhythmSlot, args.swapRhythmFile);
            swapRhythmTriggered = true;
            std::fprintf(stderr, "mu-clid render: rhythm-%d swap to %s staged at %.3fs\n",
                         args.swapRhythmSlot, args.swapRhythmFile.getFileName().toRawUTF8(),
                         written / args.sampleRate);
            std::fflush(stderr);
        }
        if (midiProgramAtSample >= 0 && ! midiProgramInjected && written >= midiProgramAtSample)
        {
            midi.addEvent(juce::MidiMessage::programChange(9, args.midiProgram), 0);
            midiProgramInjected = true;
            wasPending = true;
            std::fprintf(stderr, "mu-clid render: MIDI program change %d (ch 9) injected at %.3fs\n",
                         args.midiProgram, written / args.sampleRate);
            std::fflush(stderr);
        }
    };

    // afterBlock: the render loop never yields to JUCE's message loop, so service any
    // triggerAsyncUpdate() (e.g. a staged swap commit) synchronously, and log the commit.
    auto afterBlock = [&](int written, int ns)
    {
        proc.flushPendingAsyncUpdates();
        if (wasPending && ! proc.hasPendingFullPreset())
        {
            std::fprintf(stderr, "mu-clid render: full-preset swap committed at %.3fs\n",
                         (written + ns) / args.sampleRate);
            std::fflush(stderr);
            wasPending = false;
        }
    };

    core::renderLoop(proc, args, captured, outChannels, beforeBlock, afterBlock);

    proc.releaseResources();
    return core::writeWav(args, captured, outChannels, "mu-clid");
}

} // namespace mu_clid::render_mode
