#include "VoiceSection.h"
#include "../PluginProcessor.h"

VoiceSection::VoiceSection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &pitchOctave, &pitchSemi, &pitchFine,
                     &pitchAtk, &pitchDec, &pitchSus, &pitchRel, &pitchDepth,
                     &filterCutoff, &filterRes,
                     &filterAtk, &filterDec, &filterSus, &filterRel, &filterDepth,
                     &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent,
                     &ampAtk, &ampDec, &ampSus, &ampRel,
                     &driveDrive, &driveOutput, &driveTone })
        addAndMakeVisible(k);

    // Filter type dropdown — IDs are 1-based; filterType index = selectedId - 1.
    filterType.addItem("LP", 1);
    filterType.addItem("HP", 2);
    filterType.addItem("BP", 3);
    filterType.setSelectedId(1, false);
    addAndMakeVisible(filterType);

    // Drive character dropdown
    driveChar.addItem("Soft", 1);
    driveChar.addItem("Hard", 2);
    driveChar.addItem("Fold", 3);
    driveChar.addItem("Bit",  4);
    driveChar.setSelectedId(1, false);
    addAndMakeVisible(driveChar);

    // ─── Ranges ──────────────────────────────────────────────────────────
    pitchOctave.setRange(-4.0,   4.0,   1.0);   pitchOctave.setValue(0.0);
    pitchSemi  .setRange(-12.0, 12.0,   1.0);   pitchSemi  .setValue(0.0);
    pitchFine  .setRange(-100.0,100.0,  0.1);   pitchFine  .setValue(0.0);
    // ADSR: 0–100 display scale. 0→1 ms, 100→10 s (A/D/R); 0–100% (S).
    pitchAtk   .setRange(0.0, 100.0, 0.1);   pitchAtk  .setValue(0.0);
    pitchDec   .setRange(0.0, 100.0, 0.1);   pitchDec  .setValue(1.0);
    pitchSus   .setRange(0.0, 100.0, 0.1);   pitchSus  .setValue(0.0);
    pitchRel   .setRange(0.0, 100.0, 0.1);   pitchRel  .setValue(1.0);
    pitchDepth .setRange(0.0,  24.0, 0.1);   pitchDepth.setValue(0.0);

    filterCutoff.setRange(20.0, 20000.0, 1.0);  filterCutoff.setValue(8000.0);
    filterRes   .setRange(0.0,  100.0,  0.1);   filterRes   .setValue(20.0);
    filterAtk   .setRange(0.0,  100.0,  0.1);   filterAtk  .setValue(1.0);
    filterDec   .setRange(0.0,  100.0,  0.1);   filterDec  .setValue(3.0);
    filterSus   .setRange(0.0,  100.0,  0.1);   filterSus  .setValue(0.0);
    filterRel   .setRange(0.0,  100.0,  0.1);   filterRel  .setValue(3.0);
    filterDepth .setRange(0.0,   48.0,  0.1);   filterDepth.setValue(0.0);

    ampLevel  .setRange(0.0, 2.0,   0.01);  ampLevel  .setValue(1.0);
    ampSendEff.setRange(0.0, 1.0,  0.01);  ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 1.0,  0.01);  ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 1.0,  0.01);  ampSendRev.setValue(0.0);
    ampAccent .setRange(0.0, 12.0, 0.1);   ampAccent .setValue(0.0);
    ampAtk    .setRange(0.0, 100.0, 0.1);  ampAtk    .setValue(0.0);
    ampDec  .setRange(0.0, 100.0, 0.1);    ampDec .setValue(3.0);
    ampSus  .setRange(0.0, 100.0, 0.1);    ampSus .setValue(80.0);
    ampRel  .setRange(0.0, 100.0, 0.1);    ampRel .setValue(5.0);

    driveDrive .setRange(0.0,    100.0, 0.1);   driveDrive .setValue(0.0);
    driveOutput.setRange(-24.0,    0.0, 0.1);   driveOutput.setValue(0.0);
    driveTone  .setRange(20.0,  20000.0, 1.0);  driveTone  .setValue(20000.0);

    wireCallbacks();
}

void VoiceSection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void VoiceSection::wireCallbacks()
{
    // Status bar forwarding
    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &pitchOctave,  "Pitch Octave"            }, { &pitchSemi,    "Pitch Semitone"          },
        { &pitchFine,    "Pitch Fine"               }, { &pitchAtk,     "Pitch Attack"            },
        { &pitchDec,     "Pitch Decay"              }, { &pitchSus,     "Pitch Sustain"           },
        { &pitchRel,     "Pitch Release"            }, { &pitchDepth,   "Pitch Depth"             },
        { &filterCutoff, "Filter Cutoff"            }, { &filterRes,    "Filter Resonance"        },
        { &filterAtk,    "Filter Envelope Attack"   }, { &filterDec,    "Filter Envelope Decay"   },
        { &filterSus,    "Filter Envelope Sustain"  }, { &filterRel,    "Filter Envelope Release"  },
        { &filterDepth,  "Filter Envelope Depth"    }, { &ampLevel,     "Amp Level"               },
        { &ampSendEff,   "Amp Send Effect"          }, { &ampSendDly,   "Amp Send Delay"          },
        { &ampSendRev,   "Amp Send Reverb"          }, { &ampAccent,    "Amp Accent"               },
        { &ampAtk,       "Amp Attack"               }, { &ampDec,       "Amp Decay"               },
        { &ampSus,       "Amp Sustain"              }, { &ampRel,       "Amp Release"             },
        { &driveDrive,   "Drive"                    }, { &driveOutput,  "Drive Output"            },
        { &driveTone,    "Drive Tone"               },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }

    // Pitch config
    pitchOctave.onValueChanged = [this](double v) { apvtsSet("pitchOct",  (float)v); };
    pitchSemi  .onValueChanged = [this](double v) { apvtsSet("pitchSemi", (float)v); };
    pitchFine  .onValueChanged = [this](double v) { apvtsSet("pitchFine", (float)v); };

    // Pitch envelope
    pitchAtk  .onValueChanged = [this](double v) { apvtsSet("pEnvAtk", (float)v); };
    pitchDec  .onValueChanged = [this](double v) { apvtsSet("pEnvDec", (float)v); };
    pitchSus  .onValueChanged = [this](double v) { apvtsSet("pEnvSus", (float)v); };
    pitchRel  .onValueChanged = [this](double v) { apvtsSet("pEnvRel", (float)v); };
    pitchDepth.onValueChanged = [this](double v) { apvtsSet("pEnvDep", (float)v); };

    // Filter config — DropdownSelect uses 1-based IDs; filterType index = selectedId - 1.
    filterType.onChange = [this](int id) { apvtsSet("fltType", (float)(id - 1)); };
    filterCutoff.onValueChanged = [this](double v) { apvtsSet("fltCut", (float)v); };
    filterRes   .onValueChanged = [this](double v) { apvtsSet("fltRes", (float)(v / 100.0)); };

    // Filter envelope
    filterAtk  .onValueChanged = [this](double v) { apvtsSet("fEnvAtk", (float)v); };
    filterDec  .onValueChanged = [this](double v) { apvtsSet("fEnvDec", (float)v); };
    filterSus  .onValueChanged = [this](double v) { apvtsSet("fEnvSus", (float)v); };
    filterRel  .onValueChanged = [this](double v) { apvtsSet("fEnvRel", (float)v); };
    filterDepth.onValueChanged = [this](double v) { apvtsSet("fEnvDep", (float)v); };

    // Amp config
    ampLevel.onValueChanged = [this](double v) { apvtsSet("ampLvl", (float)v); };

    ampSendEff.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendEff"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendDly.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendDly"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendRev.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendRev"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };

    // Accent boost (Ring C coincidence)
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)v); };

    // Amp envelope
    ampAtk.onValueChanged = [this](double v) { apvtsSet("aEnvAtk", (float)v); };
    ampDec.onValueChanged = [this](double v) { apvtsSet("aEnvDec", (float)v); };
    ampSus.onValueChanged = [this](double v) { apvtsSet("aEnvSus", (float)v); };
    ampRel.onValueChanged = [this](double v) { apvtsSet("aEnvRel", (float)v); };

    // Drive — char is 0-based index; dropdown IDs are 1-based.
    driveChar  .onChange       = [this](int id) { apvtsSet("drvChar", (float)(id - 1)); updateDriveLabels(); };
    driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
    driveOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
    driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
}

void VoiceSection::setRhythm(int ri)
{
    rhythmIndex = ri;
    loadFromRhythm();
}

void VoiceSection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(rhythmIndex);
    const auto& p = r.voiceParams;

    pitchOctave .setValue(p.pitchOctave,         juce::dontSendNotification);
    pitchSemi   .setValue(p.pitchSemitones,      juce::dontSendNotification);
    pitchFine   .setValue(p.pitchFine,           juce::dontSendNotification);
    // Reverse-convert seconds → 0–100 display scale.
    pitchAtk    .setValue(p.pitchEnvAtk  * 10.0, juce::dontSendNotification);
    pitchDec    .setValue(p.pitchEnvDec  * 10.0, juce::dontSendNotification);
    pitchSus    .setValue(p.pitchEnvSus  * 100.0,juce::dontSendNotification);
    pitchRel    .setValue(p.pitchEnvRel  * 10.0, juce::dontSendNotification);
    pitchDepth  .setValue(p.pitchEnvDepth,       juce::dontSendNotification);

    filterType.setSelectedId(p.filterType + 1, false);
    filterCutoff.setValue(p.filterCutoff,           juce::dontSendNotification);
    filterRes   .setValue(p.filterRes * 100.0,      juce::dontSendNotification);
    filterAtk   .setValue(p.filterEnvAtk  * 10.0,  juce::dontSendNotification);
    filterDec   .setValue(p.filterEnvDec  * 10.0,  juce::dontSendNotification);
    filterSus   .setValue(p.filterEnvSus  * 100.0, juce::dontSendNotification);
    filterRel   .setValue(p.filterEnvRel  * 10.0,  juce::dontSendNotification);
    filterDepth .setValue(p.filterEnvDepth,         juce::dontSendNotification);

    ampLevel.setValue(p.ampLevel,               juce::dontSendNotification);

    {
        const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
        auto load = [&](KnobWithLabel& k, const char* param) {
            if (auto* raw = proc.apvts.getRawParameterValue(chPfx + param))
                k.setValue(*raw, juce::dontSendNotification);
        };
        load(ampSendEff, "sendEff");
        load(ampSendDly, "sendDly");
        load(ampSendRev, "sendRev");
    }

    ampAccent.setValue(p.accentDb,               juce::dontSendNotification);
    ampAtk  .setValue(p.ampEnvAtk  * 10.0,      juce::dontSendNotification);
    ampDec  .setValue(p.ampEnvDec  * 10.0,      juce::dontSendNotification);
    ampSus  .setValue(p.ampEnvSus  * 100.0,     juce::dontSendNotification);
    ampRel  .setValue(p.ampEnvRel  * 10.0,      juce::dontSendNotification);

    driveChar  .setSelectedId(p.driveChar + 1,       false);
    driveDrive .setValue(p.driveDrive,               juce::dontSendNotification);
    driveOutput.setValue(p.driveOutput,              juce::dontSendNotification);
    driveTone  .setValue(p.driveTone,                juce::dontSendNotification);
    updateDriveLabels();
}

void VoiceSection::updateDriveLabels()
{
    // Bitcrusher uses different parameter names; all others share the generic set.
    const bool isBit = (driveChar.getSelectedId() == 4);
    driveDrive .setLabel(isBit ? "Bits"   : "Drive");
    driveOutput.setLabel(isBit ? "Rate"   : "Output");
    driveTone  .setLabel("Tone");
}

// ─── Layout ──────────────────────────────────────────────────────────────────

void VoiceSection::resized()
{
    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int gap    = 4;
    const int rowH   = (h - labelH - gap) / 2;

    const int kW = (w - 3 * divW) / 19;   // 19 cols, 3 dividers

    const int pitchX  = 0;
    const int filterX = 5 * kW + divW;
    const int ampX    = 10 * kW + 2 * divW;
    const int driveX  = 15 * kW + 3 * divW;

    const int row1Y = labelH;
    const int row2Y = labelH + rowH + gap;

    // ─── Pitch ───────────────────────────────────────────────────────────
    pitchOctave.setBounds(pitchX + 0 * kW, row1Y, kW, rowH);
    pitchSemi  .setBounds(pitchX + 1 * kW, row1Y, kW, rowH);
    pitchFine  .setBounds(pitchX + 2 * kW, row1Y, kW, rowH);
    pitchDepth .setBounds(pitchX + 3 * kW, row1Y, kW, rowH);  // depth on config row

    pitchAtk.setBounds(pitchX + 0 * kW, row2Y, kW, rowH);
    pitchDec.setBounds(pitchX + 1 * kW, row2Y, kW, rowH);
    pitchSus.setBounds(pitchX + 2 * kW, row2Y, kW, rowH);
    pitchRel.setBounds(pitchX + 3 * kW, row2Y, kW, rowH);

    // ─── Filter ──────────────────────────────────────────────────────────
    // Dropdown half-height centred; Cutoff, Res, Depth on cols 1–3.
    filterType  .setBounds(filterX + 0 * kW, row1Y + rowH / 4, kW, rowH / 2);
    filterCutoff.setBounds(filterX + 1 * kW, row1Y, kW, rowH);
    filterRes   .setBounds(filterX + 2 * kW, row1Y, kW, rowH);
    filterDepth .setBounds(filterX + 3 * kW, row1Y, kW, rowH);  // depth on config row

    filterAtk.setBounds(filterX + 0 * kW, row2Y, kW, rowH);
    filterDec.setBounds(filterX + 1 * kW, row2Y, kW, rowH);
    filterSus.setBounds(filterX + 2 * kW, row2Y, kW, rowH);
    filterRel.setBounds(filterX + 3 * kW, row2Y, kW, rowH);

    // ─── Amp ─────────────────────────────────────────────────────────────
    ampLevel  .setBounds(ampX + 0 * kW, row1Y, kW, rowH);
    ampSendEff.setBounds(ampX + 1 * kW, row1Y, kW, rowH);
    ampSendDly.setBounds(ampX + 2 * kW, row1Y, kW, rowH);
    ampSendRev.setBounds(ampX + 3 * kW, row1Y, kW, rowH);
    ampAccent .setBounds(ampX + 4 * kW, row1Y, kW, rowH);

    ampAtk.setBounds(ampX + 0 * kW, row2Y, kW, rowH);
    ampDec.setBounds(ampX + 1 * kW, row2Y, kW, rowH);
    ampSus.setBounds(ampX + 2 * kW, row2Y, kW, rowH);
    ampRel.setBounds(ampX + 3 * kW, row2Y, kW, rowH);

    // ─── Drive ───────────────────────────────────────────────────────────
    // Algorithm dropdown spans the full section on row 1; controls on row 2.
    driveChar  .setBounds(driveX, row1Y + rowH / 4, 4 * kW, rowH / 2);
    driveDrive .setBounds(driveX + 0 * kW, row2Y, kW, rowH);
    driveOutput.setBounds(driveX + 1 * kW, row2Y, kW, rowH);
    driveTone  .setBounds(driveX + 2 * kW, row2Y, kW, rowH);
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int kW     = (w - 3 * divW) / 19;

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    const float div1X = static_cast<float>(5 * kW) + divW * 0.5f;
    const float div2X = static_cast<float>(10 * kW + divW) + divW * 0.5f;
    const float div3X = static_cast<float>(15 * kW + 2 * divW) + divW * 0.5f;
    g.drawLine(div1X, h * 0.05f, div1X, h * 0.95f, 0.5f);
    g.drawLine(div2X, h * 0.05f, div2X, h * 0.95f, 0.5f);
    g.drawLine(div3X, h * 0.05f, div3X, h * 0.95f, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("PITCH",  0,                      0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("FILTER", 5 * kW + divW,          0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("AMP",    10 * kW + 2 * divW,     0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("DRIVE",  15 * kW + 3 * divW,     0, 4 * kW, labelH, juce::Justification::centred, false);
}
