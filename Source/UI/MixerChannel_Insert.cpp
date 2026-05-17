// #408: partial-class TU split from MixerChannel.cpp. Contains the master-insert
// algorithm configuration code — the 11-case switch over insert algorithms with
// per-algorithm knob labels, ranges, formatters, and APVTS wiring (~280 lines).
// Also owns the kInsertDefaults table since it's only consumed here.
// See MixerChannel_Bindings.cpp header for the split rationale.

#include "MixerChannel.h"
#include "../PluginProcessor.h"
#include <cmath>

// First-visit defaults for each insert algorithm.  Fields map to VoiceParams members:
// driveDrive | driveOutput | drvDither | driveTone | eqMidGain | drvBits | driveRate
const MixerChannel::InsertAlgoSnapshot MixerChannel::kInsertDefaults[11] = {
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 0  None
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 1  Soft Clip  (0% drive = transparent)
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 2  Hard Clip
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 3  Fold
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 4  Bitcrusher (16-bit, 48 kHz, flat)
    { 100.0f, 0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 5  Clipper    (100% = full range, no clipping)
    { 50.0f,  0.0f, 50.0f,  1000.0f,  0.0f, 16.0f, 48000.0f },  // 6  EQ         (0 dB all bands, 1 kHz mid)
    { 30.0f,  0.0f, 0.0f,    200.0f,  0.0f, 16.0f, 48000.0f },  // 7  Compressor (−12 dB thresh, 200 ms release)
    { 30.0f,  0.0f, 0.0f,    200.0f,  0.0f, 16.0f, 48000.0f },  // 8  Limiter    (−12 dB ceiling, 200 ms)
    { 50.0f,  0.0f, 0.0f,    440.0f,  0.0f, 16.0f, 48000.0f },  // 9  Ring Mod   (50% mix, 440 Hz)
    { 0.0f,   0.0f, 0.0f,   20000.0f, 0.0f, 16.0f, 48000.0f },  // 10 Tape Sat  (0% drive = transparent)
};

void MixerChannel::configureInsertAlgorithm(int charId, int slot, PluginProcessor* proc)
{
    if (!hasInsert()) return;

    KnobWithLabel& drive  = slot == 0 ? insDrive  : insDrive2;
    KnobWithLabel& output = slot == 0 ? insOutput : insOutput2;
    KnobWithLabel& tone   = slot == 0 ? insTone   : insTone2;
    KnobWithLabel& extra  = slot == 0 ? insExtra  : insExtra2;

    // Null callbacks first — prevents spurious APVTS writes during range changes.
    drive .onValueChanged = nullptr;
    output.onValueChanged = nullptr;
    tone  .onValueChanged = nullptr;
    extra .onValueChanged = nullptr;
    output.setGRSource(nullptr);  // #246: cleared here; comp/limiter cases re-set below

    // #243: the lambda must keep working after this method is re-invoked from
    // loadFromAPVTS with proc=nullptr (the dropdown char value comes from APVTS,
    // so we don't want to write back — but the knob callbacks still need a live
    // proc handle for the user to actually drive the engine).
    PluginProcessor* const knobProc = masterInsertProc;
    const juce::String pDrv  = slot == 0 ? "mst_insDrv"  : "mst_ins2Drv";
    const juce::String pOut  = slot == 0 ? "mst_insOut"   : "mst_ins2Out";
    const juce::String pDit  = slot == 0 ? "mst_insDit"   : "mst_ins2Dit";
    const juce::String pTon  = slot == 0 ? "mst_insTon"   : "mst_ins2Ton";
    const juce::String pMid  = slot == 0 ? "mst_insMid"   : "mst_ins2Mid";
    const juce::String pBit  = slot == 0 ? "mst_insBits"  : "mst_ins2Bits";
    const juce::String pRte  = slot == 0 ? "mst_insRate"  : "mst_ins2Rate";
    const juce::String pChar = slot == 0 ? "mst_insChar"  : "mst_ins2Char";

    auto setParam = [knobProc](const juce::String& id, double v)
    {
        if (!knobProc) return;
        if (auto* p = knobProc->apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };

    const VoiceParams& ip = masterInsertProc
        ? (slot == 0 ? masterInsertProc->mixerEngine.masterInsertParams
                     : masterInsertProc->mixerEngine.masterInsertParams2)
        : VoiceParams{};

    switch (charId)
    {
        case 0: // None — hide all knobs
            drive .setVisible(false);
            output.setVisible(false);
            tone  .setVisible(false);
            extra .setVisible(false);
            if (proc) setParam(pChar, 0);
            break;

        case 1: case 2: case 3: // Soft Clip / Hard Clip / Fold
        case 5:                  // Clipper — same Drive/Output/LPF layout
            drive .setLabel(charId == 5 ? "Threshold" : "Drive");
            drive .setRange(0.0, 100.0, 0.1);
            if (charId != 5)
            {
                drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return juce::String(v * 0.4, 1) + " dB";
                };
                drive .getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                    return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
                };
            }
            else
            {
                drive .getSlider().textFromValueFunction = nullptr;
                drive .getSlider().valueFromTextFunction = nullptr;
            }
            drive .setValue(ip.driveDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("LPF");
            tone  .setRange(20.0, 20000.0, 1.0);
            tone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone  .setValue(ip.driveTone, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, charId);
            break;

        case 4: // Bitcrusher — Bits / Rate / Dither
            drive .setLabel("Bits");
            drive .setRange(1.0, 16.0, 1.0);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v) + " bits";
            };
            drive .setValue(ip.drvBits, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Rate");
            output.setRange(100.0, 48000.0, 1.0);
            output.getSlider().setSkewFactorFromMidPoint(2190.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            output.setValue(ip.driveRate, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("Dither");
            tone  .setRange(0.0, 100.0, 0.1);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            tone  .setValue(ip.drvDither, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            output.onValueChanged = [setParam, pRte](double v) { setParam(pRte, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            if (proc) setParam(pChar, 4);
            break;

        case 6: // 3-Band EQ — Low / Mid gain / Mid Hz / High (#248: 4 knobs)
        {
            auto dbFmt = [](double v) -> juce::String {
                return (v >= 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
            };
            auto hzFmt = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };

            drive .getSlider().setSkewFactor(1.0);
            output.getSlider().setSkewFactor(1.0);
            tone  .getSlider().setSkewFactor(1.0);

            drive .setLabel("Low");
            drive .setRange(-18.0, 18.0, 0.1);
            drive .getSlider().textFromValueFunction = dbFmt;
            drive .setValue(ip.driveDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Mid");
            output.setRange(-18.0, 18.0, 0.1);
            output.getSlider().textFromValueFunction = dbFmt;
            output.setValue(ip.eqMidGain, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("High");
            tone  .setRange(-18.0, 18.0, 0.1);
            tone  .getSlider().textFromValueFunction = dbFmt;
            tone  .setValue(ip.drvDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setLabel("Mid Hz");
            extra .setRange(200.0, 8000.0, 1.0);
            extra .getSlider().setSkewFactorFromMidPoint(1000.0);
            extra .getSlider().textFromValueFunction = hzFmt;
            extra .setValue(juce::jlimit(200.0, 8000.0, (double)ip.driveTone), juce::dontSendNotification);
            extra .setVisible(true);

            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, (v + 18.0) / 36.0 * 100.0); };
            output.onValueChanged = [setParam, pMid](double v) { setParam(pMid, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, (v + 18.0) / 36.0 * 100.0); };
            extra .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 6);
            break;
        }

        case 7: case 8: // Compressor / Limiter — Threshold / Output / Release
            drive .setLabel(charId == 8 ? "Ceiling" : "Threshold");
            drive .setRange(0.0, 100.0, 0.1);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4)) + " dB";
            };
            drive .setValue(ip.driveDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 24.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("Release");
            tone  .setRange(20.0, 2000.0, 1.0);
            tone  .getSlider().setSkewFactorFromMidPoint(200.0);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v < 1000.0 ? juce::String((int)v) + " ms"
                                  : juce::String(v / 1000.0, 2) + " s";
            };
            tone  .setValue(juce::jlimit(20.0, 2000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            // #246: GR meter on the Output knob
            output.setGRSource(masterInsertProc
                ? (slot == 0 ? &masterInsertProc->mixerEngine.masterInsert.grReduction
                             : &masterInsertProc->mixerEngine.masterInsert2.grReduction)
                : nullptr);
            if (proc) setParam(pChar, charId);
            break;

        case 9: // Ring Modulator — Mix + Freq
            drive.setLabel("Mix");
            drive.setRange(0.0, 100.0, 0.1);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            drive.setValue(ip.driveDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setVisible(false);

            tone.setLabel("Freq");
            tone.setRange(10.0, 5000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(223.6);  // log feel
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone.setValue(juce::jlimit(10.0, 5000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive.onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            tone .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 9);
            break;

        case 10: // Tape Saturation — Drive / Output / Tone
            drive.setLabel("Drive");
            drive.setRange(0.0, 100.0, 0.1);
            drive.getSlider().textFromValueFunction = nullptr;
            drive.setValue(ip.driveDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setLabel("Output");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = nullptr;
            output.setValue(ip.driveOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel("Tone");
            tone.setRange(200.0, 20000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(2000.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v >= 1000.0 ? juce::String(v / 1000.0, 2) + "kHz"
                                   : juce::String((int)v) + "Hz";
            };
            tone.setValue(juce::jlimit(200.0, 20000.0, (double)ip.driveTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [setParam, pTon](double v) { setParam(pTon, v); };
            if (proc) setParam(pChar, 10);
            break;

        default: break;
    }

    for (auto* k : { &drive, &output, &tone, &extra })
    { k->getSlider().updateText(); k->repaint(); }

    resized();
}

