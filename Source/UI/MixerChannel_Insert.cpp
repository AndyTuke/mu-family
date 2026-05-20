// partial-class TU split from MixerChannel.cpp. Contains the master-insert
// algorithm configuration code — the 13-case switch over insert algorithms with
// per-algorithm knob labels, ranges, formatters, and APVTS wiring (~280 lines).
// See MixerChannel_Bindings.cpp header for the split rationale.
// per-algorithm default table moved to UI/InsertAlgoDefaults.h
// (mu_ui::kInsertAlgoDefaults); shared with the per-rhythm VoiceSection page.

#include "MixerChannel.h"
#include "../Plugin/PluginProcessor.h"
#include <cmath>

namespace {
static juce::String fmtHzNum(double v)
{
    if (v < 1000.0)  return juce::String((int)std::round(v));
    if (v < 10000.0) return juce::String(v / 1000.0, 2);
    return juce::String(v / 1000.0, 1);
}
static juce::String fmtHzLabel(const juce::String& name, double v)
{
    return name + (v < 1000.0 ? " (Hz)" : " (kHz)");
}
} // namespace

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
    output.setGRSource(nullptr);  // cleared here; comp/limiter cases re-set below

    // reset any grey-out applied by a previous Vocoder noise-carrier
    // configuration so the next algorithm doesn't inherit the disabled state.
    for (auto* k : { &drive, &output, &tone, &extra })
    {
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }

    // the lambda must keep working after this method is re-invoked from
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
            drive .setLabel(charId == 5 ? "Threshold" : "Drive (dB)");
            drive .setRange(0.0, 100.0, 0.1);
            if (charId != 5)
            {
                drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return juce::String(v * 0.4, 1);
                };
                drive .getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                    return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
                };
            }
            else
            {
                drive .getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String((int)std::round(v)); };
                drive .getSlider().valueFromTextFunction = nullptr;
            }
            drive .setValue(ip.insertDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output (dB)");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            output.setValue(ip.insertOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel(fmtHzLabel("LPF", (double)ip.insertTone));
            tone  .setRange(20.0, 20000.0, 1.0);
            tone  .getSlider().setSkewFactorFromMidPoint(640.0);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            tone  .setValue(ip.insertTone, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insTone : insTone2).setLabel(fmtHzLabel("LPF", v)); };
            if (proc) setParam(pChar, charId);
            break;

        case 4: // Bitcrusher — Bits / Rate / Dither
            drive .setLabel("Bits");
            drive .setRange(1.0, 16.0, 1.0);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v);
            };
            drive .setValue(ip.insertBits, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel(fmtHzLabel("Rate", (double)ip.insertRate));
            output.setRange(100.0, 48000.0, 1.0);
            output.getSlider().setSkewFactorFromMidPoint(2190.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            output.setValue(ip.insertRate, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("Dither");
            tone  .setRange(0.0, 100.0, 0.1);
            tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            tone  .setValue(ip.insertDither, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            output.onValueChanged = [this, slot, setParam, pRte](double v) { setParam(pRte, v); (slot == 0 ? insOutput : insOutput2).setLabel(fmtHzLabel("Rate", v)); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            if (proc) setParam(pChar, 4);
            break;

        case 6: // 3-Band EQ — Low / Mid gain / Mid Hz / High (#248: 4 knobs)
        {
            auto dbFmt = [](double v) -> juce::String {
                return juce::String(v, 1);
            };

            drive .getSlider().setSkewFactor(1.0);
            output.getSlider().setSkewFactor(1.0);
            tone  .getSlider().setSkewFactor(1.0);

            drive .setLabel("Low (dB)");
            drive .setRange(-18.0, 18.0, 0.1);
            drive .getSlider().textFromValueFunction = dbFmt;
            drive .setValue(ip.insertDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Mid (dB)");
            output.setRange(-18.0, 18.0, 0.1);
            output.getSlider().textFromValueFunction = dbFmt;
            output.setValue(ip.insertEqMid, juce::dontSendNotification);
            output.setVisible(true);

            tone  .setLabel("High (dB)");
            tone  .setRange(-18.0, 18.0, 0.1);
            tone  .getSlider().textFromValueFunction = dbFmt;
            tone  .setValue(ip.insertDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            tone  .setVisible(true);

            extra .setLabel(fmtHzLabel("Mid", juce::jlimit(200.0, 8000.0, (double)ip.insertTone)));
            extra .setRange(200.0, 8000.0, 1.0);
            extra .getSlider().setSkewFactorFromMidPoint(1000.0);
            extra .getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            extra .setValue(juce::jlimit(200.0, 8000.0, (double)ip.insertTone), juce::dontSendNotification);
            extra .setVisible(true);

            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, (v + 18.0) / 36.0 * 100.0); };
            output.onValueChanged = [setParam, pMid](double v) { setParam(pMid, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, (v + 18.0) / 36.0 * 100.0); };
            extra .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insExtra : insExtra2).setLabel(fmtHzLabel("Mid", v)); };
            if (proc) setParam(pChar, 6);
            break;
        }

        case 7: case 8: // Compressor / Limiter — Threshold / Output / Release
            drive .setLabel(charId == 8 ? "Ceiling (dB)" : "Threshold (dB)");
            drive .setRange(0.0, 100.0, 0.1);
            drive .getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4));
            };
            drive .setValue(ip.insertDrive, juce::dontSendNotification);
            drive .setVisible(true);

            output.setLabel("Output (dB)");
            output.setRange(-24.0, 24.0, 0.1);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            output.setValue(ip.insertOutput, juce::dontSendNotification);
            output.setVisible(true);

            {
                const double toneInitVal = juce::jlimit(20.0, 2000.0, (double)ip.insertTone);
                tone  .setLabel(toneInitVal < 1000.0 ? "Release (ms)" : "Release (s)");
                tone  .setRange(20.0, 2000.0, 1.0);
                tone  .getSlider().setSkewFactorFromMidPoint(200.0);
                tone  .getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return v < 1000.0 ? juce::String((int)v) : juce::String(v / 1000.0, 2);
                };
                tone  .setValue(toneInitVal, juce::dontSendNotification);
                tone  .setVisible(true);
            }

            extra .setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insTone : insTone2).setLabel(v < 1000.0 ? "Release (ms)" : "Release (s)"); };
            // GR meter on the Output knob
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
                return juce::String((int)std::round(v));
            };
            drive.setValue(ip.insertDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setVisible(false);

            tone.setLabel(fmtHzLabel("Freq", juce::jlimit(10.0, 5000.0, (double)ip.insertTone)));
            tone.setRange(10.0, 5000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(223.6);  // log feel
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            tone.setValue(juce::jlimit(10.0, 5000.0, (double)ip.insertTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive.onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            tone .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insTone : insTone2).setLabel(fmtHzLabel("Freq", v)); };
            if (proc) setParam(pChar, 9);
            break;

        case 10: // Tape Saturation — Drive / Output / Tone
            drive.setLabel("Drive");
            drive.setRange(0.0, 100.0, 0.1);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String((int)std::round(v)); };
            drive.setValue(ip.insertDrive, juce::dontSendNotification);
            drive.setVisible(true);

            output.setLabel("Output (dB)");
            output.setRange(-24.0, 0.0, 0.1);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            output.setValue(ip.insertOutput, juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel(fmtHzLabel("Tone", juce::jlimit(200.0, 20000.0, (double)ip.insertTone)));
            tone.setRange(200.0, 20000.0, 1.0);
            tone.getSlider().setSkewFactorFromMidPoint(2000.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            tone.setValue(juce::jlimit(200.0, 20000.0, (double)ip.insertTone), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setVisible(false);
            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v); };
            tone  .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insTone : insTone2).setLabel(fmtHzLabel("Tone", v)); };
            if (proc) setParam(pChar, 10);
            break;

        case 11:  // ── Karplus-Strong — Note / Octave / Feedback / LPF ─
        {
            static const char* const kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

            drive.setLabel("Note");
            drive.setRange(0.0, 11.0, 1.0);
            drive.getSlider().setSkewFactor(1.0);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 11, (int) std::round(v))];
            };
            drive.setValue(juce::jlimit(0.0, 11.0, (double) ip.insertDrive), juce::dontSendNotification);
            drive.setVisible(true);

            output.setLabel("Octave");
            // range extended to 0..3.
            output.setRange(0.0, 3.0, 1.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            output.setValue(juce::jlimit(0.0, 3.0, (double) ip.insertBits), juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel("Feedback");
            tone.setRange(0.0, 100.0, 0.1);
            tone.getSlider().setSkewFactor(1.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            tone.setValue(juce::jlimit(0.0, 100.0, (double) ip.insertDither), juce::dontSendNotification);
            tone.setVisible(true);

            // follow-up: LPF cutoff on the feedback path — user can
            // dial the brightness / damping. insertTone (20..20k) maps directly.
            extra.setLabel(fmtHzLabel("LPF", juce::jlimit(20.0, 20000.0, (double)ip.insertTone)));
            extra.setRange(20.0, 20000.0, 1.0);
            extra.getSlider().setSkewFactorFromMidPoint(640.0);
            extra.getSlider().textFromValueFunction = [](double v) -> juce::String { return fmtHzNum(v); };
            extra.setValue(juce::jlimit(20.0, 20000.0, (double) ip.insertTone), juce::dontSendNotification);
            extra.setVisible(true);

            drive .onValueChanged = [setParam, pDrv](double v) { setParam(pDrv, v); };
            output.onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            extra .onValueChanged = [this, slot, setParam, pTon](double v) { setParam(pTon, v); (slot == 0 ? insExtra : insExtra2).setLabel(fmtHzLabel("LPF", v)); };
            if (proc) setParam(pChar, 11);
            break;
        }

        case 12:  // ── Vocoder — Wave / Unison / Octave / Note ────────
        {
            static const char* const kWaveNames[4] = { "Saw", "Square", "White", "Pink" };
            static const char* const kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            static const int kUnisonCounts[7] = { 1, 3, 5, 7, 9, 11, 13 };

            drive.setLabel("Wave");
            drive.setRange(0.0, 3.0, 1.0);
            drive.getSlider().setSkewFactor(1.0);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kWaveNames[juce::jlimit(0, 3, (int) std::round(v))];
            };
            drive.setValue(juce::jlimit(0.0, 3.0, (double) ip.insertDrive), juce::dontSendNotification);
            drive.setVisible(true);

            // Unison now uses insertOutput field (range -24..0). UI knob
            // value is 0..6; encode/decode via `±24` offset and /4 scale.
            output.setLabel("Unison");
            output.setRange(0.0, 6.0, 1.0);
            output.getSlider().setSkewFactor(1.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(kUnisonCounts[juce::jlimit(0, 6, (int) std::round(v))]);
            };
            const double unisonKnob = juce::jlimit(0.0, 6.0,
                                                  ((double) ip.insertOutput + 24.0) * 0.25);
            output.setValue(unisonKnob, juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel("Octave");
            tone.setRange(1.0, 5.0, 1.0);
            tone.getSlider().setSkewFactor(1.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            tone.setValue(juce::jlimit(1.0, 5.0, (double) ip.insertDither), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setLabel("Note");
            extra.setRange(1.0, 12.0, 1.0);
            extra.getSlider().setSkewFactor(1.0);
            extra.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 11, (int) std::round(v) - 1)];
            };
            extra.setValue(juce::jlimit(1.0, 12.0, (double) ip.insertBits), juce::dontSendNotification);
            extra.setVisible(true);

            // grey out Unison / Octave / Note when carrier is
            // noise (waveshape 2 / 3) — algorithm ignores them. Capture `this`
            // and `slot` so the lambda can look up the right knob pair every
            // call (the local refs above only live until configure() returns).
            auto syncVocoderGreyOut = [this, slot]
            {
                KnobWithLabel& d = slot == 0 ? insDrive  : insDrive2;
                KnobWithLabel& o = slot == 0 ? insOutput : insOutput2;
                KnobWithLabel& t = slot == 0 ? insTone   : insTone2;
                KnobWithLabel& e = slot == 0 ? insExtra  : insExtra2;
                const int wave = juce::jlimit(0, 3, (int) std::round(d.getValue()));
                const bool pitched = (wave < 2);
                o.setEnabled(pitched);
                t.setEnabled(pitched);
                e.setEnabled(pitched);
                o.setAlpha(pitched ? 1.0f : 0.45f);
                t.setAlpha(pitched ? 1.0f : 0.45f);
                e.setAlpha(pitched ? 1.0f : 0.45f);
            };
            syncVocoderGreyOut();

            drive .onValueChanged = [setParam, pDrv, syncVocoderGreyOut](double v)
            {
                setParam(pDrv, v);
                syncVocoderGreyOut();
            };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v * 4.0 - 24.0); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            extra .onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            if (proc) setParam(pChar, 12);
            break;
        }

        case 13:  // ── Vocoder Stereo — same controls as Vocoder (12) ────
        {
            static const char* const kWaveNames[4] = { "Saw", "Square", "White", "Pink" };
            static const char* const kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
            static const int kUnisonCounts[7] = { 1, 3, 5, 7, 9, 11, 13 };

            drive.setLabel("Wave");
            drive.setRange(0.0, 3.0, 1.0);
            drive.getSlider().setSkewFactor(1.0);
            drive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kWaveNames[juce::jlimit(0, 3, (int) std::round(v))];
            };
            drive.setValue(juce::jlimit(0.0, 3.0, (double) ip.insertDrive), juce::dontSendNotification);
            drive.setVisible(true);

            output.setLabel("Unison");
            output.setRange(0.0, 6.0, 1.0);
            output.getSlider().setSkewFactor(1.0);
            output.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(kUnisonCounts[juce::jlimit(0, 6, (int) std::round(v))]);
            };
            const double unisonKnob = juce::jlimit(0.0, 6.0,
                                                  ((double) ip.insertOutput + 24.0) * 0.25);
            output.setValue(unisonKnob, juce::dontSendNotification);
            output.setVisible(true);

            tone.setLabel("Octave");
            tone.setRange(1.0, 5.0, 1.0);
            tone.getSlider().setSkewFactor(1.0);
            tone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            tone.setValue(juce::jlimit(1.0, 5.0, (double) ip.insertDither), juce::dontSendNotification);
            tone.setVisible(true);

            extra.setLabel("Note");
            extra.setRange(1.0, 12.0, 1.0);
            extra.getSlider().setSkewFactor(1.0);
            extra.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 11, (int) std::round(v) - 1)];
            };
            extra.setValue(juce::jlimit(1.0, 12.0, (double) ip.insertBits), juce::dontSendNotification);
            extra.setVisible(true);

            auto syncVocoderStGreyOut = [this, slot]
            {
                KnobWithLabel& d = slot == 0 ? insDrive  : insDrive2;
                KnobWithLabel& o = slot == 0 ? insOutput : insOutput2;
                KnobWithLabel& t = slot == 0 ? insTone   : insTone2;
                KnobWithLabel& e = slot == 0 ? insExtra  : insExtra2;
                const int wave = juce::jlimit(0, 3, (int) std::round(d.getValue()));
                const bool pitched = (wave < 2);
                o.setEnabled(pitched);
                t.setEnabled(pitched);
                e.setEnabled(pitched);
                o.setAlpha(pitched ? 1.0f : 0.45f);
                t.setAlpha(pitched ? 1.0f : 0.45f);
                e.setAlpha(pitched ? 1.0f : 0.45f);
            };
            syncVocoderStGreyOut();

            drive .onValueChanged = [setParam, pDrv, syncVocoderStGreyOut](double v)
            {
                setParam(pDrv, v);
                syncVocoderStGreyOut();
            };
            output.onValueChanged = [setParam, pOut](double v) { setParam(pOut, v * 4.0 - 24.0); };
            tone  .onValueChanged = [setParam, pDit](double v) { setParam(pDit, v); };
            extra .onValueChanged = [setParam, pBit](double v) { setParam(pBit, v); };
            if (proc) setParam(pChar, 13);
            break;
        }

        default: break;
    }

    for (auto* k : { &drive, &output, &tone, &extra })
    { k->getSlider().updateText(); k->repaint(); }

    resized();
}

