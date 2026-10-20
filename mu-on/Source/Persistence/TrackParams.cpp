#include "Persistence/TrackParams.h"
#include "MuLimits.h"   // mu-core: mu_limits::kMaxLayers (the Rumble source's range)

namespace mu_on
{

void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot)
{
    using namespace juce;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };

    const String lbl = "T" + String(slot + 1) + " ";
    auto id = [&](EngineType e, const char* p) { return ParameterID{ engineParamId(slot, e, p), 1 }; };
    auto flt = [&](EngineType e, const char* p, const String& name, NormalisableRange<float> r, float def)
    {
        layout.add(std::make_unique<AudioParameterFloat>(id(e, p), lbl + name, r, def));
    };
    using E = EngineType;

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ layerParamId(slot, "eng"), 1 }, lbl + "Engine", engineNames(), 0));
    layout.add(std::make_unique<AudioParameterInt>   (ParameterID{ layerParamId(slot, "src"), 1 }, lbl + "Rumble Source", 0, mu_limits::kMaxLayers - 1, 0));

    // Kick engine (synthesis)
    flt(E::Kick, "tune",  "Kick Tune",        f(30.0f, 120.0f, 0.1f), 50.0f);
    flt(E::Kick, "ptch",  "Kick Pitch Amt",   f(0.0f, 600.0f, 1.0f), 220.0f);
    flt(E::Kick, "pdec",  "Kick Pitch Decay", f(5.0f, 200.0f, 0.1f), 50.0f);
    flt(E::Kick, "adec",  "Kick Decay",       f(20.0f, 800.0f, 1.0f), 180.0f);
    flt(E::Kick, "drive", "Kick Drive",       f(0.0f, 1.0f, 0.001f), 0.2f);

    // Bass engine (deep synth — the focus)
    layout.add(std::make_unique<AudioParameterChoice>(id(E::Bass, "wave"), lbl + "Bass Wave", juce::StringArray{ "Sine", "Saw", "Square" }, 0));
    flt(E::Bass, "sub",   "Bass Sub",        f(0.0f, 1.0f, 0.001f), 0.5f);
    flt(E::Bass, "tune",  "Bass Tune",       f(20.0f, 120.0f, 0.1f), 41.2f);
    flt(E::Bass, "cut",   "Bass Cutoff",     f(40.0f, 4000.0f, 1.0f), 600.0f);
    flt(E::Bass, "res",   "Bass Resonance",  f(0.0f, 1.0f, 0.001f), 0.2f);
    flt(E::Bass, "env",   "Bass Filter Env", f(0.0f, 1.0f, 0.001f), 0.4f);
    flt(E::Bass, "edec",  "Bass Env Decay",  f(10.0f, 1000.0f, 1.0f), 180.0f);
    flt(E::Bass, "atk",   "Bass Attack",     f(0.5f, 200.0f, 0.1f), 2.0f);
    flt(E::Bass, "dec",   "Bass Decay",      f(20.0f, 1500.0f, 1.0f), 200.0f);
    flt(E::Bass, "sus",   "Bass Sustain",    f(0.0f, 1.0f, 0.001f), 0.0f);
    flt(E::Bass, "drive", "Bass Drive",      f(0.0f, 1.0f, 0.001f), 0.2f);

    // Hat / Snare (sample engines, different defaults)
    flt(E::Hat,   "tune", "Hat Tune",    f(-12.0f, 12.0f, 0.1f), 0.0f);
    flt(E::Hat,   "dec",  "Hat Decay",   f(10.0f, 400.0f, 1.0f), 60.0f);
    flt(E::Snare, "tune", "Snare Tune",  f(-12.0f, 12.0f, 0.1f), 0.0f);
    flt(E::Snare, "dec",  "Snare Decay", f(20.0f, 600.0f, 1.0f), 160.0f);

    // Rumble engine (processes another track's audio: drive → delays → reverb → env → filter)
    flt(E::Rumble, "drive",  "Rumble Drive",     f(0.0f, 1.0f, 0.001f), 0.4f);
    flt(E::Rumble, "d1",     "Rumble 1/16",      f(0.0f, 1.0f, 0.001f), 0.5f);
    flt(E::Rumble, "d2",     "Rumble 2/16",      f(0.0f, 1.0f, 0.001f), 0.35f);
    flt(E::Rumble, "d3",     "Rumble 3/16",      f(0.0f, 1.0f, 0.001f), 0.25f);
    flt(E::Rumble, "size",   "Rumble Rev Size",  f(0.0f, 1.0f, 0.001f), 0.7f);
    flt(E::Rumble, "revmix", "Rumble Rev Mix",   f(0.0f, 1.0f, 0.001f), 0.5f);
    flt(E::Rumble, "revlp",  "Rumble Rev LP",    NormalisableRange<float>(200.0f, 18000.0f, 1.0f, 0.3f), 1200.0f);
    flt(E::Rumble, "cut",    "Rumble Cutoff",    NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 800.0f);
    flt(E::Rumble, "res",    "Rumble Resonance", f(0.0f, 1.0f, 0.001f), 0.2f);

    // The track's own groove: its swing and the velocity of its accented steps. Not modulation targets.
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "swing"), 1 }, lbl + "Swing",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "accent"), 1 }, lbl + "Accent",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
}

void remapRumbleSources(juce::AudioProcessorValueTreeState& apvts, int liveCount,
                        const std::function<EngineType(int)>& engineOfSlot,
                        const std::function<int(int)>& oldToNew)
{
    // The first Kick in the new order (a source that is gone falls back to it).
    int fallback = liveCount;
    for (int j = 0; j < mu_limits::kMaxLayers; ++j)
        if (engineOfSlot(j) == EngineType::Kick)
        {
            const int nj = oldToNew(j);
            if (nj >= 0 && nj < liveCount) fallback = juce::jmin(fallback, nj);
        }
    if (fallback >= liveCount) fallback = 0;

    // Every Rumble slot's source, rewritten at its old slot.
    for (int j = 0; j < mu_limits::kMaxLayers; ++j)
    {
        if (engineOfSlot(j) != EngineType::Rumble) continue;
        auto* p = apvts.getParameter(layerParamId(j, "src"));
        if (p == nullptr) continue;
        const int oldSrc = juce::roundToInt(p->convertFrom0to1(p->getValue()));
        int newSrc = oldToNew(oldSrc);
        if (newSrc < 0) newSrc = fallback;
        if (newSrc != oldSrc) p->setValueNotifyingHost(p->convertTo0to1((float) newSrc));
    }
}

} // namespace mu_on
