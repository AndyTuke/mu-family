#include "Persistence/TrackParams.h"
#include "MuLimits.h"   // mu-core: mu_limits::kMaxLayers (the Rumble source's range)
#include "Plugin/MixerFxParams.h"
#include "Persistence/ParamText.h"   // mu-core: mu_fmt::floatUnit   // mu-core: addInsertParams

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

    // A range skewed so `centre` sits mid-knob: the part of a time or frequency range that is played with (short
    // decays, low cutoffs) gets most of the travel, as in mu-Clid's envelope times (skew 0.3) and cutoff (centre 640 Hz).
    auto centred = [](float lo, float hi, float step, float centre)
    {
        NormalisableRange<float> r(lo, hi, step);
        r.setSkewForCentre(centre);
        return r;
    };

    const String lbl = "T" + String(slot + 1) + " ";
    auto id = [&](EngineType e, const char* p) { return ParameterID{ engineParamId(slot, e, p), 1 }; };
    auto flt = [&](EngineType e, const char* p, const String& name, NormalisableRange<float> r, float def, mu_fmt::Unit unit = mu_fmt::Unit::Plain)
    {
        layout.add(std::make_unique<AudioParameterFloat>(id(e, p), lbl + name, r, def, mu_fmt::floatUnit(r, unit)));   // the status bar and the host read "180 ms", "1.3 kHz", "50 %"
    };
    using E = EngineType;

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID{ layerParamId(slot, "eng"), 1 }, lbl + "Engine", engineNames(), 0));
    layout.add(std::make_unique<AudioParameterInt>   (ParameterID{ layerParamId(slot, "src"), 1 }, lbl + "Rumble Source", 0, mu_limits::kMaxLayers - 1, 0));

    // Kick engine (synthesis)
    flt(E::Kick, "tune",  "Kick Tune",        pitchRange(30.0f, 120.0f), 50.0f, mu_fmt::Unit::HzNote);   // snapped to the master key and scale by the engine
    flt(E::Kick, "ptch",  "Kick Pitch Amt",   centred(0.0f, 600.0f, 1.0f, 150.0f), 220.0f, mu_fmt::Unit::Hz);
    flt(E::Kick, "pdec",  "Kick Pitch Decay", centred(5.0f, 200.0f, 0.1f, 30.0f), 50.0f, mu_fmt::Unit::Ms);
    flt(E::Kick, "adec",  "Kick Decay",       centred(20.0f, 800.0f, 1.0f, 150.0f), 180.0f, mu_fmt::Unit::Ms);
    flt(E::Kick, "drive", "Kick Drive",       f(0.0f, 1.0f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);
    flt(E::Kick, "fine",  "Kick Fine",        f(-100.0f, 100.0f, 0.1f), 0.0f, mu_fmt::Unit::Cents);   // cents off the locked note (not a modulation target)

    // Bass engine (deep synth — the focus)
    layout.add(std::make_unique<AudioParameterChoice>(id(E::Bass, "wave"), lbl + "Bass Wave", juce::StringArray{ "Sine", "Saw", "Square" }, 0));
    flt(E::Bass, "sub",   "Bass Sub",        f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::Unit::Percent01);
    {   // snapped to the master key and scale by the engine; at its default it reads "Root" and plays the key's root note
        const auto r = pitchRange(20.0f, 120.0f);
        layout.add(std::make_unique<AudioParameterFloat>(id(E::Bass, "tune"), lbl + "Bass Tune", r, kBassTuneRootHz,
            mu_fmt::floatText(r, [](float hz, int) { return isBassTuneRoot(hz) ? juce::String("Root") : mu_fmt::unitText(mu_fmt::Unit::HzNote, hz); })));
    }
    flt(E::Bass, "cut",   "Bass Cutoff",     pitchRange(40.0f, 4000.0f), 600.0f, mu_fmt::Unit::Hz);
    flt(E::Bass, "res",   "Bass Resonance",  f(0.0f, 1.0f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);
    flt(E::Bass, "env",   "Bass Filter Env", f(0.0f, 1.0f, 0.001f), 0.4f, mu_fmt::Unit::Percent01);
    flt(E::Bass, "edec",  "Bass Env Decay",  centred(10.0f, 1000.0f, 1.0f, 100.0f), 180.0f, mu_fmt::Unit::Ms);
    flt(E::Bass, "atk",   "Bass Attack",     centred(0.5f, 200.0f, 0.1f, 10.0f), 2.0f, mu_fmt::Unit::Ms);
    flt(E::Bass, "dec",   "Bass Decay",      centred(20.0f, 1500.0f, 1.0f, 200.0f), 200.0f, mu_fmt::Unit::Ms);
    flt(E::Bass, "sus",   "Bass Sustain",    f(0.0f, 1.0f, 0.001f), 0.0f, mu_fmt::Unit::Percent01);
    flt(E::Bass, "drive", "Bass Drive",      f(0.0f, 1.0f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);
    flt(E::Bass, "rel",   "Bass Release",    centred(5.0f, 2000.0f, 1.0f, 150.0f), 300.0f, mu_fmt::Unit::Ms);      // after the step's gate
    flt(E::Bass, "glide", "Bass Glide",      centred(0.0f, 500.0f, 1.0f, 80.0f), 80.0f, mu_fmt::Unit::Ms);       // the slide into a tied step
    flt(E::Bass, "fine",  "Bass Fine",       f(-100.0f, 100.0f, 0.1f), 0.0f, mu_fmt::Unit::Cents);   // cents off the locked note (not a modulation target)

    // Hat / Snare (sample engines, different defaults)
    flt(E::Hat,   "tune", "Hat Tune",    f(-12.0f, 12.0f, 0.1f), 0.0f, mu_fmt::Unit::Semitones);
    flt(E::Hat,   "dec",  "Hat Decay",   centred(10.0f, 400.0f, 1.0f, 60.0f), 60.0f, mu_fmt::Unit::Ms);
    flt(E::Hat,   "otune", "Hat Open Tune",  f(-12.0f, 12.0f, 0.1f), 0.0f, mu_fmt::Unit::Semitones);    // the open hat: its own tune and decay
    flt(E::Hat,   "odec",  "Hat Open Decay", centred(50.0f, 1200.0f, 1.0f, 250.0f), 300.0f, mu_fmt::Unit::Ms);
    flt(E::Snare, "tune", "Snare Tune",  f(-12.0f, 12.0f, 0.1f), 0.0f, mu_fmt::Unit::Semitones);
    flt(E::Snare, "dec",  "Snare Decay", centred(20.0f, 600.0f, 1.0f, 120.0f), 160.0f, mu_fmt::Unit::Ms);
    flt(E::Snare, "snap", "Snare Snap",  f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::Unit::Percent01);   // wires against body; 0.5 is the original blend

    // Rumble engine (processes another track's audio: drive → delays → reverb → env → filter)
    flt(E::Rumble, "drive",  "Rumble Drive",     f(0.0f, 1.0f, 0.001f), 0.4f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "d1",     "Rumble 1/16",      f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "d2",     "Rumble 2/16",      f(0.0f, 1.0f, 0.001f), 0.35f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "d3",     "Rumble 3/16",      f(0.0f, 1.0f, 0.001f), 0.25f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "size",   "Rumble Rev Size",  f(0.0f, 1.0f, 0.001f), 0.7f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "revmix", "Rumble Rev Mix",   f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "revlp",  "Rumble Rev LP",    NormalisableRange<float>(200.0f, 18000.0f, 1.0f, 0.3f), 1200.0f, mu_fmt::Unit::Hz);
    flt(E::Rumble, "cut",    "Rumble Cutoff",    centred(20.0f, 20000.0f, 1.0f, 640.0f), 800.0f, mu_fmt::Unit::Hz);
    flt(E::Rumble, "res",    "Rumble Resonance", f(0.0f, 1.0f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);
    flt(E::Rumble, "inlp",   "Rumble Input LP",  centred(20.0f, 20000.0f, 1.0f, 640.0f), 20000.0f, mu_fmt::Unit::Hz);   // fully open = off

    // The track's insert effect: algorithm and the four generic knobs (the shared insert layout, as a mixer bus insert).
    mu_mixfx::addInsertParams(layout, layerParamPrefix(slot), lbl);   // t{N}_drvChar, t{N}_insP1..4: the ids the shared InsertSubsection reads

    // The track's own groove: its swing and the velocity of its accented steps. Not modulation targets.
    // Probability: the share of the track's notes that play (0..100 %, every note, accents included). It multiplies each
    // step's own probability (StepRow).
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "level"), 1 }, lbl + "Level", f(0.0f, 1.0f, 0.001f), 1.0f,
                                                     mu_fmt::floatUnit(f(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));   // the track's own level, a modulation target (the mixer fader is not)
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "prob"), 1 }, lbl + "Probability",
                                                     NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
                                                     AudioParameterFloatAttributes().withStringFromValueFunction(
                                                         [](float v, int) { return String(roundToInt(v)) + " %"; })));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "swing"), 1 }, lbl + "Swing",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
                                                     mu_fmt::floatUnit(NormalisableRange<float>(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "accent"), 1 }, lbl + "Accent",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
                                                     mu_fmt::floatUnit(NormalisableRange<float>(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));

    // FM Perc engine (phase-modulation toms and percussion), appended after the groove so every earlier row keeps
    // its place in saved data and host parameter order. Tune and Ratio are absolute; the other floats are
    // normalised 0..1 amounts that each algorithm scales through its own table.
    layout.add(std::make_unique<AudioParameterChoice>(id(E::FmPerc, "algo"), lbl + "FM Perc Algorithm", fmPercAlgorithmNames(), 0));
    flt(E::FmPerc, "tune",  "FM Perc Tune",      NormalisableRange<float>(40.0f, 4000.0f, 0.1f, 0.4f), 90.0f, mu_fmt::Unit::Hz);
    flt(E::FmPerc, "ratio", "FM Perc Ratio",     NormalisableRange<float>(0.5f, 6.0f, 0.01f, 0.6f), 1.5f, mu_fmt::Unit::Ratio);
    flt(E::FmPerc, "index", "FM Perc Index",     f(0.0f, 1.0f, 0.001f), 0.35f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "idec",  "FM Perc FM Env",    f(0.0f, 1.0f, 0.001f), 0.57f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "fdbk",  "FM Perc Feedback",  f(0.0f, 1.0f, 0.001f), 0.05f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "dive",  "FM Perc Dive",      f(0.0f, 1.0f, 0.001f), 0.43f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "dtime", "FM Perc Dive Time", f(0.0f, 1.0f, 0.001f), 0.46f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "decay", "FM Perc Decay",     f(0.0f, 1.0f, 0.001f), 0.40f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "fold",  "FM Perc Fold",      f(0.0f, 1.0f, 0.001f), 0.15f, mu_fmt::Unit::Percent01);
    flt(E::FmPerc, "noise", "FM Perc Noise",     f(0.0f, 1.0f, 0.001f), 0.15f, mu_fmt::Unit::Percent01);

    // Chord engine (a chord player for dub-techno stabs), appended after FM Perc. The chord itself is per step, in the step row.
    layout.add(std::make_unique<AudioParameterChoice>(id(E::Chord, "wave"), lbl + "Chord Wave", juce::StringArray{ "Saw", "Square", "Pulse" }, 0));
    flt(E::Chord, "cut",   "Chord Cutoff",     pitchRange(40.0f, 8000.0f), 900.0f, mu_fmt::Unit::Hz);
    flt(E::Chord, "res",   "Chord Resonance",  f(0.0f, 1.0f, 0.001f), 0.25f, mu_fmt::Unit::Percent01);
    flt(E::Chord, "drive", "Chord Drive",      f(0.0f, 1.0f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);       // on the filter's input
    flt(E::Chord, "env",   "Chord Filter Env", f(0.0f, 1.0f, 0.001f), 0.5f, mu_fmt::Unit::Percent01);
    flt(E::Chord, "edec",  "Chord Env Decay",  centred(10.0f, 1500.0f, 1.0f, 100.0f), 300.0f, mu_fmt::Unit::Ms);
    flt(E::Chord, "atk",   "Chord Attack",     centred(0.5f, 500.0f, 0.1f, 10.0f), 3.0f, mu_fmt::Unit::Ms);
    flt(E::Chord, "dec",   "Chord Decay",      centred(20.0f, 2000.0f, 1.0f, 200.0f), 400.0f, mu_fmt::Unit::Ms);
    flt(E::Chord, "sus",   "Chord Sustain",    f(0.0f, 1.0f, 0.001f), 0.6f, mu_fmt::Unit::Percent01);
    flt(E::Chord, "rel",   "Chord Release",    centred(5.0f, 3000.0f, 1.0f, 150.0f), 250.0f, mu_fmt::Unit::Ms);

    // Ghost level, appended after the engines: the velocity of a ghost step (drum and percussion engines) as a share of
    // a normal note's.
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{ layerParamId(slot, "ghost"), 1 }, lbl + "Ghost",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f,
                                                     mu_fmt::floatUnit(NormalisableRange<float>(0.0f, 1.0f, 0.001f), mu_fmt::Unit::Percent01)));

    // The drum and percussion filter (Kick, Hat, Snare, FM Perc): type, cutoff and resonance, after the voice. Open (the
    // cutoff at its top, low-pass) leaves the sound untouched.
    auto drumFilter = [&](EngineType e, const String& name)
    {
        layout.add(std::make_unique<AudioParameterChoice>(id(e, "ftype"), lbl + name + " Filter Type", juce::StringArray{ "LP12", "HP12", "BP12", "Notch12" }, 0));
        flt(e, "fcut", name + " Filter Cutoff",    centred(20.0f, 20000.0f, 1.0f, 640.0f), 20000.0f, mu_fmt::Unit::Hz);
        flt(e, "fres", name + " Filter Resonance", f(0.0f, 0.99f, 0.001f), 0.2f, mu_fmt::Unit::Percent01);
        // The filter's AD envelope, restarted by every step: the attack and decay times and the depth it moves the cutoff by
        // (up for positive, down for negative; full depth is kDrumFilterEnvOctaves octaves).
        flt(e, "fatk", name + " Filter Env Attack", centred(0.5f, 200.0f, 0.1f, 10.0f), 0.5f, mu_fmt::Unit::Ms);
        flt(e, "fdec", name + " Filter Env Decay",  centred(10.0f, 2000.0f, 1.0f, 150.0f), 150.0f, mu_fmt::Unit::Ms);
        flt(e, "fdep", name + " Filter Env Depth",  f(-1.0f, 1.0f, 0.001f), 0.0f, mu_fmt::Unit::Percent01);
    };
    drumFilter(E::Kick, "Kick");  drumFilter(E::Hat, "Hat");  drumFilter(E::Snare, "Snare");  drumFilter(E::FmPerc, "FM Perc");
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
