// mu-clid-midi-shot: renders the real mu-Clid MIDI editor to a PNG, for the website screenshots.
//
// The plug-in has no Standalone, so this builds the real PluginProcessor and PluginEditor into a
// console app and snapshots the editor off-screen at its own size and scale 1.0.
//
// Build:  cmake --build build --config Release --target mu-clid-midi-shot
// Run:    mu-clid-midi-shot.exe <output.png>
// Exit:   0 on success, non-zero with a message on stderr otherwise.
//
// Test-only: EXCLUDE_FROM_ALL, never part of a release, installer or CI.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

#include <cstdio>

namespace {

// Sets a parameter by its real (denormalised) value, the way the host would.
void setReal(juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

// The default state is a plain 4-hits-in-16 rhythm with no rotation and the accent at 0, which
// shows little of what the product does. When the rhythm is still exactly that, set a typical
// Euclidean groove (7 hits in 16 steps, rotated by two, accent at 60%) so the screenshot shows an
// uneven, recognisable pattern and a lit accent knob. A loaded state is left alone.
void applyDemoSettingsIfDefault(juce::AudioProcessorValueTreeState& apvts)
{
    const auto raw = [&apvts](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    if (raw("r0_stepsA") != 16.0f || raw("r0_hitsA") != 4.0f || raw("r0_rotA") != 0.0f || raw("lite_accentAmt") != 0.0f)
        return;

    setReal(apvts, "r0_hitsA", 7.0f);
    setReal(apvts, "r0_rotA", 2.0f);
    setReal(apvts, "lite_accentAmt", 60.0f);
}

// Runs the message loop briefly so queued parameter listeners and timers (the processor applies
// parameter changes to the rhythm from a timer) take effect before the next step.
void pumpMessages(int milliseconds)
{
    struct Stopper : juce::Timer { void timerCallback() override { juce::MessageManager::getInstance()->stopDispatchLoop(); } } stopper;
    stopper.startTimer(milliseconds);
    juce::MessageManager::getInstance()->runDispatchLoop();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: mu-clid-midi-shot <output.png>\n");
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    mu_clid_midi::PluginProcessor processor;
    applyDemoSettingsIfDefault(processor.apvts);
    pumpMessages(300);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
    {
        std::fprintf(stderr, "createEditor returned null\n");
        return 3;
    }

    // Let the editor's first refresh and layout settle before the snapshot.
    pumpMessages(200);

    const auto bounds = editor->getLocalBounds();
    const juce::Image image = editor->createComponentSnapshot(bounds, false, 1.0f);
    if (! image.isValid())
    {
        std::fprintf(stderr, "snapshot failed\n");
        return 4;
    }

    const juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (! stream.openedOk())
    {
        std::fprintf(stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8());
        return 5;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream(image, stream))
    {
        std::fprintf(stderr, "PNG write failed\n");
        return 6;
    }

    std::printf("%dx%d -> %s\n", image.getWidth(), image.getHeight(), out.getFullPathName().toRawUTF8());
    editor.reset();
    return 0;
}
