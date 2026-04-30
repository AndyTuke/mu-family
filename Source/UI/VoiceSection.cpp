#include "VoiceSection.h"

VoiceSection::VoiceSection()
{
    for (auto* k : { &ampAttack, &ampDecay, &ampSustain, &ampRelease,
                     &filterCutoff, &filterRes,
                     &fEnvAttack, &fEnvDecay, &fEnvDepth })
        addAndMakeVisible(k);
    addAndMakeVisible(outputMode);

    ampAttack.setRange(0.001, 5.0, 0.001);  ampAttack.setValue(0.005);
    ampDecay.setRange(0.001, 5.0, 0.001);   ampDecay.setValue(0.3);
    ampSustain.setRange(0.0, 1.0, 0.001);   ampSustain.setValue(0.8);
    ampRelease.setRange(0.001, 10.0, 0.001);ampRelease.setValue(0.5);

    filterCutoff.setRange(20.0, 20000.0, 1.0); filterCutoff.setValue(8000.0);
    filterRes.setRange(0.0, 1.0, 0.01);        filterRes.setValue(0.2);

    fEnvAttack.setRange(0.001, 5.0, 0.001); fEnvAttack.setValue(0.01);
    fEnvDecay.setRange(0.001, 5.0, 0.001);  fEnvDecay.setValue(0.2);
    fEnvDepth.setRange(0.0, 1.0, 0.01);     fEnvDepth.setValue(0.0);

    wireStatusCallbacks();
}

void VoiceSection::wireStatusCallbacks()
{
    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &ampAttack,   "Amp Attack"    }, { &ampDecay,   "Amp Decay"    },
        { &ampSustain,  "Amp Sustain"   }, { &ampRelease, "Amp Release"  },
        { &filterCutoff,"Filter Cutoff" }, { &filterRes,  "Filter Res"   },
        { &fEnvAttack,  "Filt Env Atk"  }, { &fEnvDecay,  "Filt Env Dec" },
        { &fEnvDepth,   "Filt Env Dep"  },
    };
    for (auto& e : entries)
    {
        juce::String name(e.name);
        e.k->onStatusUpdate = [this, name](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(name, val);
        };
    }
}

void VoiceSection::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Knob groups: 4 (amp) + div + 2 (filter) + div + 3 (fenv) + div + mode
    const int nKnobs = 9;
    const int nDivs  = 3;
    const int modeW  = 52;
    const int divW   = 6;
    const int knobW  = (w - nDivs * divW - modeW - divW) / nKnobs;

    // Cap knob height at 75px (80% of 90px Euclid cell) so all non-euclid knobs are the same size
    const int knobH = juce::jmin(h, 75);
    const int knobY = (h - knobH) / 2;

    int x = 0;
    auto place = [&](juce::Component& c, int cw) { c.setBounds(x, knobY, cw, knobH); x += cw; };
    auto gap   = [&]() { x += divW; };

    place(ampAttack,   knobW); place(ampDecay,  knobW);
    place(ampSustain,  knobW); place(ampRelease, knobW);
    gap();
    place(filterCutoff, knobW); place(filterRes, knobW);
    gap();
    place(fEnvAttack, knobW); place(fEnvDecay, knobW); place(fEnvDepth, knobW);
    gap();
    outputMode.setBounds(x, (h - 28) / 2, modeW, 28);
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w = getWidth();
    const int h = getHeight();
    const int nKnobs = 9;
    const int nDivs  = 3;
    const int modeW  = 52;
    const int divW   = 6;
    const int knobW  = (w - nDivs * divW - modeW - divW) / nKnobs;

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));

    // Dividers after knob groups: after amp (4 knobs), filter (2), fenv (3)
    auto drawDiv = [&](int xPos) {
        g.drawLine((float)(xPos + divW / 2), h * 0.2f, (float)(xPos + divW / 2), h * 0.8f, 0.5f);
    };
    drawDiv(4 * knobW);
    drawDiv(4 * knobW + divW + 2 * knobW);
    drawDiv(4 * knobW + divW + 2 * knobW + divW + 3 * knobW);
}
