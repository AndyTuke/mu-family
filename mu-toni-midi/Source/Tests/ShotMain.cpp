// mu-toni-midi-shot: renders the real mu-Toni MIDI editor to a PNG, for the website screenshots.
//
// The plug-in has no Standalone, so this builds the real PluginProcessor and PluginEditor into a console app and snapshots the
// editor off-screen at its own size and scale 1.0.
//
// Build:  cmake --build build --config Release --target mu-toni-midi-shot
// Run:    mu-toni-midi-shot.exe <output.png>
// Test-only: EXCLUDE_FROM_ALL, never part of a release, installer or CI.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

#include <cstdio>

namespace {

void setReal(juce::AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(value));
}

// A few settings and one modulator, so the picture shows what the product does rather than an empty default.
void applyDemoSettings(mu_toni_midi::PluginProcessor& processor)
{
    setReal(processor.apvts, "a0_acc", 70.0f);
    setReal(processor.apvts, "a0_accPat", 17.0f);
    setReal(processor.apvts, "a0_octs", 3.0f);

    auto& layer = processor.getCore().getLayer();
    auto& cs = layer.controlSequences[0];
    cs.mode = ControlSequence::Mode::Stepped;
    cs.stepValues.assign((size_t) cs.getStepCount(), 0.0f);
    for (size_t i = 0; i < cs.stepValues.size(); ++i) cs.stepValues[i] = (float) ((i * 37) % 100);
    ModulationAssignment a;
    a.id = "demo"; a.sourceId = "cs0_output"; a.destinationId = "arp.dir"; a.depth = 60.0f;
    layer.modulationMatrix.addAssignment(a);
}

void pumpMessages(int milliseconds)
{
    struct Stopper : juce::Timer { void timerCallback() override { juce::MessageManager::getInstance()->stopDispatchLoop(); } } stopper;
    stopper.startTimer(milliseconds);
    juce::MessageManager::getInstance()->runDispatchLoop();
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: mu-toni-midi-shot <output.png>\n"); return 2; }

    juce::ScopedJuceInitialiser_GUI gui;
    mu_toni_midi::PluginProcessor processor;
    applyDemoSettings(processor);
    pumpMessages(300);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr) { std::fprintf(stderr, "createEditor returned null\n"); return 3; }
    pumpMessages(300);

    const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
    if (! image.isValid()) { std::fprintf(stderr, "snapshot failed\n"); return 4; }

    const juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (! stream.openedOk()) { std::fprintf(stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8()); return 5; }
    juce::PNGImageFormat png;
    if (! png.writeImageToStream(image, stream)) { std::fprintf(stderr, "PNG write failed\n"); return 6; }

    std::printf("%dx%d -> %s\n", image.getWidth(), image.getHeight(), out.getFullPathName().toRawUTF8());
    editor.reset();
    return 0;
}
