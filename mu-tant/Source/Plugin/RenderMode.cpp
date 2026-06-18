#include "RenderMode.h"
#include "Plugin/PluginProcessor.h"

namespace mu_tant::render_mode {

namespace core = mu_core::render_mode;

Args parse(const juce::String& commandLine)
{
    Args a;
    auto tokens = juce::StringArray::fromTokens(commandLine, true);
    if (! tokens.contains("--render"))
        return a;
    tokens.removeString("--render");

    core::parseCommon(tokens, a, "mu-tant");   // sets a.valid + clamps; reports if --out missing
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

    juce::AudioBuffer<float> captured(outChannels, totalSamples);
    captured.clear();

    core::renderLoop(proc, args, captured, outChannels);   // plain block-by-block render

    proc.releaseResources();
    return core::writeWav(args, captured, outChannels, "mu-tant");
}

} // namespace mu_tant::render_mode
