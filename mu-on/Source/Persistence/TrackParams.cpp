#include "Persistence/TrackParams.h"
#include "MuLimits.h"   // mu-core: mu_limits::kMaxLayers (the Rumble source's range)
#include "Plugin/MixerFxParams.h"   // mu-core: addInsertParams

namespace mu_on
{

juce::StringArray loopRatioNames()
{
    juce::StringArray names;
    for (const auto& r : kLoopRatios) names.add(juce::String(r.play) + ":" + juce::String(r.of));
    return names;
}

void addLayerParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slot)
{
    using namespace juce;
    auto f = [](float lo, float hi, float step) { return NormalisableRange<float>(lo, hi, step); };
    // A pitch in Hz whose knob travel is even in semitones (exponential), so every note takes the same turn and a
    // modulation depth steps through the same number of notes anywhere on the knob.
    auto pitchRange = [](float lo, float hi)
    {
        return NormalisableRange<float>(lo, hi,
            [](float a, float b, float v) { return a * std::pow(b / a, v); },
            [](float a, float b, float v) { return std::log(v / a) / std::log(b / a); },
            [](float a, float b, float v) { return juce::jlimit(a, b, v); });
    };

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
    flt(E::Kick, "tune",  "Kick Tune",        pitchRange(30.0f, 120.0f), 50.0f);   // snapped to the master key and scale by the engine
    flt(E::Kick, "ptch",  "Kick Pitch Amt",   f(0.0f, 600.0f, 1.0f), 220.0f);
    flt(E::Kick, "pdec",  "Kick Pitch Decay", f(5.0f, 200.0f, 0.1f), 50.0f);
    flt(E::Kick, "adec",  "Kick Decay",       f(20.0f, 800.0f, 1.0f), 180.0f);
    flt(E::Kick, "drive", "Kick Drive",       f(0.0f, 1.0f, 0.001f), 0.2f);
    flt(E::Kick, "fine",  "Kick Fine",        f(-100.0f, 100.0f, 0.1f), 0.0f);   // cents off the locked note (not a modulation target)

    // Bass engine (deep synth — the focus)
    layout.add(std::make_unique<AudioParameterChoice>(id(E::Bass, "wave"), lbl + "Bass Wave", juce::StringArray{ "Sine", "Saw", "Square" }, 0));
    flt(E::Bass, "sub",   "Bass Sub",        f(0.0f, 1.0f, 0.001f), 0.5f);
    flt(E::Bass, "tune",  "Bass Tune",       pitchRange(20.0f, 120.0f), 41.2f);   // snapped to the master key and scale by the engine
    flt(E::Bass, "cut",   "Bass Cutoff",     f(40.0f, 4000.0f, 1.0f), 600.0f);
    flt(E::Bass, "res",   "Bass Resonance",  f(0.0f, 1.0f, 0.001f), 0.2f);
    flt(E::Bass, "env",   "Bass Filter Env", f(0.0f, 1.0f, 0.001f), 0.4f);
    flt(E::Bass, "edec",  "Bass Env Decay",  f(10.0f, 1000.0f, 1.0f), 180.0f);
    flt(E::Bass, "atk",   "Bass Attack",     f(0.5f, 200.0f, 0.1f), 2.0f);
    flt(E::Bass, "dec",   "Bass Decay",      f(20.0f, 1500.0f, 1.0f), 200.0f);
    flt(E::Bass, "sus",   "Bass Sustain",    f(0.0f, 1.0f, 0.001f), 0.0f);
    flt(E::Bass, "drive", "Bass Drive",      f(0.0f, 1.0f, 0.001f), 0.2f);
    flt(E::Bass, "rel",   "Bass Release",    f(5.0f, 2000.0f, 1.0f), 300.0f);      // after the step's gate
    flt(E::Bass, "glide", "Bass Glide",      f(0.0f, 500.0f, 1.0f), 80.0f);       // the slide into a tied step
    flt(E::Bass, "fine",  "Bass Fine",       f(-100.0f, 100.0f, 0.1f), 0.0f);   // cents off the locked note (not a modulation target)

    // Hat / Snare (sample engines, different defaults)
    flt(E::Hat,   "tune", "Hat Tune",    f(-12.0f, 12.0f, 0.1f), 0.0f);
    flt(E::Hat,   "dec",  "Hat Decay",   f(10.0f, 400.0f, 1.0f), 60.0f);
    flt(E::Hat,   "otune", "Hat Open Tune",  f(-12.0f, 12.0f, 0.1f), 0.0f);    // the open hat: its own tune and decay
    flt(E::Hat,   "odec",  "Hat Open Decay", f(50.0f, 1200.0f, 1.0f), 300.0f);
    flt(E::Snare, "tune", "Snare Tune",  f(-12.0f, 12.0f, 0.1f), 0.0f);
    flt(E::Snare, "dec",  "Snare Decay", f(20.0f, 600.0f, 1.0f), 160.0f);
    flt(E::Snare, "snap", "Snare Snap",  f(0.0f, 1.0f, 0.001f), 0.5f);   // wires against body; 0.5 is the original blend

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
    flt(E::Rumble, "inlp",   "Rumble Input LP",  NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 20000.0f);   // fully open = off

    // The track's insert effect: algorithm and the four generic knobs (the shared insert layout, as a mixer bus insert).
    mu_mixfx::addInsertParams(layout, layerParamPrefix(slot), lbl);   // t{N}_drvChar, t{N}_insP1..4: the ids the shared InsertSubsection reads

    // The track's own groove: its swing and the velocity of its accented steps. Not modulation targets.
    // Probability: the share of the track's notes that play (0..100 %, every note, accents included). It multiplies each
    // step's own probability (StepRow).
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "level"), 1 }, lbl + "Level", f(0.0f, 1.0f, 0.001f), 1.0f));   // the track's own level, a modulation target (the mixer fader is not)
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "prob"), 1 }, lbl + "Probability",
                                                     NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
                                                     AudioParameterFloatAttributes().withStringFromValueFunction(
                                                         [](float v, int) { return String(roundToInt(v)) + " %"; })));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "swing"), 1 }, lbl + "Swing",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "accent"), 1 }, lbl + "Accent",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    // FM Perc engine (phase-modulation toms and percussion), appended after the groove so every earlier row keeps
    // its place in saved data and host parameter order. Tune and Ratio are absolute; the other floats are
    // normalised 0..1 amounts that each algorithm scales through its own table.
    layout.add(std::make_unique<AudioParameterChoice>(id(E::FmPerc, "algo"), lbl + "FM Perc Algorithm", fmPercAlgorithmNames(), 0));
    flt(E::FmPerc, "tune",  "FM Perc Tune",      NormalisableRange<float>(40.0f, 4000.0f, 0.1f, 0.4f), 90.0f);
    flt(E::FmPerc, "ratio", "FM Perc Ratio",     NormalisableRange<float>(0.5f, 6.0f, 0.01f, 0.6f), 1.5f);
    flt(E::FmPerc, "index", "FM Perc Index",     f(0.0f, 1.0f, 0.001f), 0.35f);
    flt(E::FmPerc, "idec",  "FM Perc FM Env",    f(0.0f, 1.0f, 0.001f), 0.57f);
    flt(E::FmPerc, "fdbk",  "FM Perc Feedback",  f(0.0f, 1.0f, 0.001f), 0.05f);
    flt(E::FmPerc, "dive",  "FM Perc Dive",      f(0.0f, 1.0f, 0.001f), 0.43f);
    flt(E::FmPerc, "dtime", "FM Perc Dive Time", f(0.0f, 1.0f, 0.001f), 0.46f);
    flt(E::FmPerc, "decay", "FM Perc Decay",     f(0.0f, 1.0f, 0.001f), 0.40f);
    flt(E::FmPerc, "fold",  "FM Perc Fold",      f(0.0f, 1.0f, 0.001f), 0.15f);
    flt(E::FmPerc, "noise", "FM Perc Noise",     f(0.0f, 1.0f, 0.001f), 0.15f);
}

void remapRumbleSources(juce::AudioProcessorValueTreeState& apvts, int liveCount,
                        const std::function<EngineType(int)>& engineOfSlot,
                        const std::function<int(int)>& oldToNew,
                        const std::function<bool(int)>& fallbackCandidate)
{
    // The first fallback candidate in the new order (a source that is gone falls back to it): a Kick unless the host
    // names its own candidates.
    int fallback = liveCount;
    for (int j = 0; j < mu_limits::kMaxLayers; ++j)
        if (fallbackCandidate ? fallbackCandidate(j) : engineOfSlot(j) == EngineType::Kick)
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
