#pragma once

#include "Plugin/RenderSupport.h"
#include "Plugin/ProcessorBase.h"

// The family's headless `--render` mode, built on the ProcessorBase preset API so every product
// gets the same flags (the listening-test pipeline drives them all the same way):
//
//   --render --out <wav> [--seconds N --samplerate SR --blocksize BS]
//   --preset <file> [--preset-slot N]        full preset (.<full ext>) or slot preset (.<slot ext>)
//   --swap-preset <file> --swap-at <s>       full preset loaded mid-render (staged to its boundary)
//   --swap-slot-preset <file> --swap-slot N --swap-slot-at <s>   slot preset staged mid-render
//   --midi-program P --midi-program-preset <file> --midi-program-at <s>   ch-9 program change
//   --play / --no-play                       start the internal transport (product default otherwise)
//   --save-preset <file>                     after the start preset loads, save the state as a full
//                                            preset at <file> (the listening tests' round trip)
//
// mu-Clid's original --swap-rhythm-preset / --swap-rhythm-slot / --swap-rhythm-at spellings are
// accepted as aliases. Header-only and Standalone-target-only, like RenderSupport.h.
namespace mu_core::render_mode
{
    struct ProductArgs : BaseArgs
    {
        juce::File presetFile;
        int        presetSlot = 0;

        juce::File swapPresetFile;
        double     swapAtSeconds = -1.0;

        juce::File swapSlotFile;
        int        swapSlot          = 0;
        double     swapSlotAtSeconds = -1.0;

        juce::File midiProgramPreset;
        int        midiProgram          = -1;
        double     midiProgramAtSeconds = -1.0;

        int        play = -1;   // -1 = product default, 0 = --no-play, 1 = --play

        juce::File savePresetFile;
    };

    // The value of the first of `flags` present (each removed from `tokens`).
    inline juce::String takeFirstFlagValue(juce::StringArray& tokens, std::initializer_list<const char*> flags)
    {
        juce::String v;
        for (const char* f : flags)
        {
            const auto x = takeFlagValue(tokens, f);
            if (v.isEmpty()) v = x;
        }
        return v;
    }

    // Parse a `--render` command line; `valid` only when --render (and --out) is present.
    inline ProductArgs parseProduct(const juce::String& commandLine, const char* product)
    {
        ProductArgs a;
        auto tokens = juce::StringArray::fromTokens(commandLine, true);
        if (! tokens.contains("--render"))
            return a;
        tokens.removeString("--render");

        // Product-level flags first, then the shared --out/--seconds/--samplerate/--blocksize.
        const auto preset      = takeFlagValue(tokens, "--preset");
        const auto presetSlot  = takeFlagValue(tokens, "--preset-slot");
        const auto swapPreset  = takeFlagValue(tokens, "--swap-preset");
        const auto swapAt      = takeFlagValue(tokens, "--swap-at");
        const auto swapSlotF   = takeFirstFlagValue(tokens, { "--swap-slot-preset", "--swap-rhythm-preset" });
        const auto swapSlot    = takeFirstFlagValue(tokens, { "--swap-slot",        "--swap-rhythm-slot" });
        const auto swapSlotAt  = takeFirstFlagValue(tokens, { "--swap-slot-at",     "--swap-rhythm-at" });
        const auto midiProg    = takeFlagValue(tokens, "--midi-program");
        const auto midiProgPre = takeFlagValue(tokens, "--midi-program-preset");
        const auto midiProgAt  = takeFlagValue(tokens, "--midi-program-at");
        const auto savePreset  = takeFlagValue(tokens, "--save-preset");
        if (tokens.contains("--play"))    { a.play = 1; tokens.removeString("--play"); }
        if (tokens.contains("--no-play")) { a.play = 0; tokens.removeString("--no-play"); }

        if (! parseCommon(tokens, a, product))
            return a;

        const auto cwd = juce::File::getCurrentWorkingDirectory();
        if (preset.isNotEmpty())      a.presetFile        = cwd.getChildFile(preset);
        if (swapPreset.isNotEmpty())  a.swapPresetFile    = cwd.getChildFile(swapPreset);
        if (swapSlotF.isNotEmpty())   a.swapSlotFile      = cwd.getChildFile(swapSlotF);
        if (midiProgPre.isNotEmpty()) a.midiProgramPreset = cwd.getChildFile(midiProgPre);
        if (savePreset.isNotEmpty())  a.savePresetFile    = cwd.getChildFile(savePreset);
        if (presetSlot.isNotEmpty())  a.presetSlot           = presetSlot.getIntValue();
        if (swapAt.isNotEmpty())      a.swapAtSeconds        = swapAt.getDoubleValue();
        if (swapSlot.isNotEmpty())    a.swapSlot             = swapSlot.getIntValue();
        if (swapSlotAt.isNotEmpty())  a.swapSlotAtSeconds    = swapSlotAt.getDoubleValue();
        if (midiProg.isNotEmpty())    a.midiProgram          = midiProg.getIntValue();
        if (midiProgAt.isNotEmpty())  a.midiProgramAtSeconds = midiProgAt.getDoubleValue();

        // Each optional action needs both a target and a time; if either is missing, disable it.
        if (a.swapPresetFile == juce::File{} || a.swapAtSeconds < 0.0)         a.swapAtSeconds = -1.0;
        if (a.swapSlotFile == juce::File{}   || a.swapSlotAtSeconds < 0.0)     a.swapSlotAtSeconds = -1.0;
        if (a.midiProgramPreset == juce::File{} || a.midiProgram < 0
                                                || a.midiProgramAtSeconds < 0.0) a.midiProgramAtSeconds = -1.0;
        return a;
    }

    // Load a full or slot preset by its extension. Returns false (after reporting) on a bad file.
    inline bool loadPresetByExtension(ProcessorBase& proc, const juce::File& f, int slot, const char* product)
    {
        if (! f.existsAsFile())
        {
            reportError(product, "preset file not found: " + f.getFullPathName());
            return false;
        }
        const auto ext = f.getFileExtension().toLowerCase();
        if (ext == "." + proc.getFullPresetExtension().toLowerCase())         proc.loadPreset(f);
        else if (ext == "." + proc.getPerSlotPresetExtension().toLowerCase()) proc.loadSlotPreset(slot, f);
        else
        {
            reportError(product, "unrecognised preset extension: " + ext + " (expected ."
                        + proc.getFullPresetExtension() + " or ." + proc.getPerSlotPresetExtension() + ")");
            return false;
        }
        return true;
    }

    // Run the render on a constructed processor (the caller sets ProcessorBase::skipAutoLoadDefault
    // before constructing it, so renders start from the factory state). `playByDefault` is the
    // product's transport choice when neither --play nor --no-play is given.
    inline int runProduct(ProcessorBase& proc, const ProductArgs& args, const char* product, bool playByDefault)
    {
        // Phase 1: surface load errors, load the requested preset (saving it back if asked), seed the
        // ch-9 program map.
        proc.onLoadError = [product](const juce::String& m)
        { std::fputs((juce::String(product) + " render: load: " + m + "\n").toRawUTF8(), stderr); std::fflush(stderr); };

        if (args.presetFile != juce::File{} && ! loadPresetByExtension(proc, args.presetFile, args.presetSlot, product))
            return 2;

        if (args.savePresetFile != juce::File{} && ! proc.saveFullPresetTo(args.savePresetFile))
        {
            reportError(product, "could not save preset: " + args.savePresetFile.getFullPathName());
            return 2;
        }

        if (args.midiProgramAtSeconds >= 0.0)
        {
            // setPresetPath auto-saves, but with no storage file set that is a no-op (no disk write).
            proc.midiFullPresetMap.setEnabled(true);
            proc.midiFullPresetMap.setPresetPath(args.midiProgram, args.midiProgramPreset);
        }

        // Phase 2: prepare and start the internal transport (the standalone has no host transport).
        proc.setPlayConfigDetails(0, 2, args.sampleRate, args.blockSize);
        proc.prepareToPlay(args.sampleRate, args.blockSize);
        const bool play = args.play < 0 ? playByDefault : args.play == 1;
        if (play && ! proc.isInternalPlaying())
            proc.toggleInternalPlay();

        const int totalSamples = (int) std::ceil(args.seconds * args.sampleRate);
        const int outChannels  = juce::jmax(proc.getTotalNumOutputChannels(), 2);
        juce::AudioBuffer<float> captured(outChannels, totalSamples);
        captured.clear();

        // Phase 3: render, firing each mid-render action once when its time is reached.
        auto atSample = [&](double s) { return s >= 0.0 ? (int) std::round(s * args.sampleRate) : -1; };
        const int swapAt = atSample(args.swapAtSeconds), slotAt = atSample(args.swapSlotAtSeconds),
                  progAt = atSample(args.midiProgramAtSeconds);
        bool swapDone = false, slotDone = false, progDone = false, fullPending = false;

        auto log = [&](const juce::String& what, int sample)
        {
            std::fprintf(stderr, "%s render: %s at %.3fs\n", product, what.toRawUTF8(), sample / args.sampleRate);
            std::fflush(stderr);
        };

        auto beforeBlock = [&](int written, juce::MidiBuffer& midi)
        {
            if (swapAt >= 0 && ! swapDone && written >= swapAt)
            {
                proc.loadPreset(args.swapPresetFile);   // stages; commits at the next boundary
                swapDone = fullPending = true;
                log("swap to " + args.swapPresetFile.getFileName() + " requested", written);
            }
            if (slotAt >= 0 && ! slotDone && written >= slotAt)
            {
                proc.loadSlotPreset(args.swapSlot, args.swapSlotFile);
                slotDone = true;
                log("slot-" + juce::String(args.swapSlot) + " swap to " + args.swapSlotFile.getFileName() + " staged", written);
            }
            if (progAt >= 0 && ! progDone && written >= progAt)
            {
                midi.addEvent(juce::MidiMessage::programChange(9, args.midiProgram), 0);
                progDone = fullPending = true;
                log("MIDI program change " + juce::String(args.midiProgram) + " (ch 9) injected", written);
            }
        };

        // The loop never yields to the message loop, so run the async commit work synchronously.
        auto afterBlock = [&](int written, int ns)
        {
            proc.flushPendingAsyncUpdates();
            if (fullPending && ! proc.hasPendingFullPreset())
            {
                log("full-preset swap committed", written + ns);
                fullPending = false;
            }
        };

        renderLoop(proc, args, captured, outChannels, beforeBlock, afterBlock);

        proc.releaseResources();
        return writeWav(args, captured, outChannels, product);
    }
}
