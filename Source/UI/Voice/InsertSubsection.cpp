#include "InsertSubsection.h"
#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulationSnapshot.h"
#include "Sequencer/Rhythm.h"

namespace {
static juce::String fmtHz(double v)
{
    return v < 1000.0 ? juce::String((int)std::round(v)) + " Hz"
                      : juce::String(v / 1000.0, 1) + " kHz";
}
static double parseHz(const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    if (t.endsWith("khz")) return t.dropLastCharacters(3).trim().getDoubleValue() * 1000.0;
    if (t.endsWith("hz"))  return t.dropLastCharacters(2).trim().getDoubleValue();
    return t.getDoubleValue();
}
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

InsertSubsection::InsertSubsection(PluginProcessor& p) : proc(p)
{
    insertAlgo.addItem("None",        1);
    insertAlgo.addItem("3-Band EQ",   7);
    insertAlgo.addItem("Bitcrusher",  5);
    insertAlgo.addItem("Clipper",     6);
    insertAlgo.addItem("Compressor",  8);
    insertAlgo.addItem("Fold",        4);
    insertAlgo.addItem("Hard Clip",   3);
    insertAlgo.addItem("Karplus",    12);
    insertAlgo.addItem("Limiter",     9);
    insertAlgo.addItem("Ring Mod",   10);
    insertAlgo.addItem("Soft Clip",   2);
    insertAlgo.addItem("Tape Sat",   11);
    insertAlgo.addItem("Vocoder",    13);
    insertAlgo.addItem("Vocoder St", 14);
    insertAlgo.setSelectedId(1, false);
    addAndMakeVisible(insertAlgo);

    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
        addAndMakeVisible(k);

    insertDrive .setRange(0.0,   100.0, 0.1);   insertDrive .setValue(0.0);
    insertOutput.setRange(-24.0,   0.0, 0.1);   insertOutput.setValue(0.0);
    insertDither.setRange(0.0,   100.0, 0.1);   insertDither.setValue(0.0);
    insertTone  .setRange(20.0, 20000.0, 1.0);  insertTone  .setValue(20000.0);
    insertTone  .getSlider().setSkewFactorFromMidPoint(640.0);

    wireCallbacks();
}

void InsertSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void InsertSubsection::wireCallbacks()
{
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
    {
        k->onStatusUpdate = [this, k](const juce::String& label, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate("Insert " + label, val);
        };
    }

    insertAlgo.onChange = [this](int id) {
        const int newChar = id - 1;
        const int oldChar = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                            ? proc.getRhythm(rhythmIndex).voiceParams.insertAlgo : -1;

        if (oldChar >= 0 && oldChar <= 10 && rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
        {
            const auto& vp          = proc.getRhythm(rhythmIndex).voiceParams;
            auto&       snap        = insertSnapshots[oldChar];
            snap.insertDrive         = vp.insertDrive;
            snap.insertOutput        = vp.insertOutput;
            snap.insertDither        = vp.insertDither;
            snap.insertTone          = vp.insertTone;
            snap.insertEqMid         = vp.insertEqMid;
            snap.insertBits          = vp.insertBits;
            snap.insertRate          = vp.insertRate;
            insertSnapshotValid[oldChar] = true;
        }

        const InsertAlgoSnapshot& snap = insertSnapshotValid[newChar]
                                        ? insertSnapshots[newChar]
                                        : mu_ui::kInsertAlgoDefaults[newChar];
        apvtsSet("drvDrv",    snap.insertDrive);
        apvtsSet("drvOut",    snap.insertOutput);
        apvtsSet("drvDit",    snap.insertDither);
        apvtsSet("drvTon",    snap.insertTone);
        apvtsSet("eqMidGain", snap.insertEqMid);
        apvtsSet("drvBits",   snap.insertBits);
        apvtsSet("drvRate",   snap.insertRate);
        apvtsSet("drvChar",   (float)newChar);
        configureInsertAlgorithm(newChar);
        if (onStatusUpdate) onStatusUpdate("Insert Algorithm", insertAlgo.getText());
    };
}

void InsertSubsection::setRhythm(int ri)
{
    rhythmIndex = ri;
    std::fill(std::begin(insertSnapshotValid), std::end(insertSnapshotValid), false);
    loadFromRhythm();
    refreshModulatedIndicators();
}

void InsertSubsection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    insertAlgo.setSelectedId(p.insertAlgo + 1, false);
    configureInsertAlgorithm(p.insertAlgo);
}

void InsertSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;

    if (suffix == "drvChar"
     || suffix == "drvDrv" || suffix == "drvOut" || suffix == "drvDit" || suffix == "drvTon"
     || suffix == "drvBits" || suffix == "drvRate" || suffix == "eqMidGain")
    {
        if (suffix == "drvChar") insertAlgo.setSelectedId(p.insertAlgo + 1, false);
        configureInsertAlgorithm(p.insertAlgo);
    }
}

void InsertSubsection::refreshModulatedIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& assigns = proc.getRhythm(rhythmIndex).modulationMatrix.getAssignments();

    auto isAssigned = [&assigns](const char* destId) -> bool {
        for (const auto& a : assigns)
            if (a.destinationId == destId) return true;
        return false;
    };

    auto sn = [&](int i) { return proc.getModSnapshot(rhythmIndex, i); };
    const float kNaN   = std::numeric_limits<float>::quiet_NaN();
    const bool playing = proc.sequencerPlaying.load();

    insertDrive  .setIsModulated(playing && (isAssigned("insert.drive") || isAssigned("insert.bits")));
    insertOutput .setIsModulated(playing && (isAssigned("insert.output") || isAssigned("insert.rate")));
    insertDither .setIsModulated(playing && isAssigned("insert.dither"));
    insertTone   .setIsModulated(playing && isAssigned("insert.lpf"));

    insertDrive  .setModulatedNorm(playing ? (isAssigned("insert.drive") ? sn(kSnapInsDrive)
                                           : isAssigned("insert.bits")  ? sn(kSnapInsBits) : kNaN)
                                          : kNaN);
    insertOutput .setModulatedNorm((isAssigned("insert.output") && playing) ? sn(kSnapInsOutput) : kNaN);
    insertDither .setModulatedNorm((isAssigned("insert.dither") && playing) ? sn(kSnapInsDither) : kNaN);
    insertTone   .setModulatedNorm((isAssigned("insert.lpf")    && playing) ? sn(kSnapInsLpf)    : kNaN);
}

void InsertSubsection::configureInsertAlgorithm(int charId)
{
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
        k->onValueChanged = nullptr;
    insertOutput.setGRSource(nullptr);

    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
    {
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }

    const VoiceParams* p = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                           ? &proc.getRhythm(rhythmIndex).voiceParams : nullptr;

    switch (charId)
    {
        case 0:
            insertDrive .setVisible(false);
            insertOutput.setVisible(false);
            insertDither.setVisible(false);
            insertTone  .setVisible(false);
            break;

        case 1: case 2: case 3:
            insertDrive.setLabel("Drive (dB)");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(v * 0.4, 1);
            };
            insertDrive.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
            };
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output (dB)");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel(fmtHzLabel("LPF", p ? (double)p->insertTone : 20000.0));
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); insertTone.setLabel(fmtHzLabel("LPF", v)); };
            break;

        case 4:
            insertDrive.setLabel("Bits");
            insertDrive.setRange(1.0, 16.0, 1.0);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v);
            };
            insertDrive.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return juce::jlimit(1.0, 16.0, s.retainCharacters("0123456789").getDoubleValue());
            };
            if (p) insertDrive.setValue(p->insertBits, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel(fmtHzLabel("Rate", p ? (double)p->insertRate : 48000.0));
            insertOutput.setRange(100.0, 48000.0, 1.0);
            insertOutput.getSlider().setSkewFactorFromMidPoint(2190.0);
            insertOutput.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertOutput.getSlider().valueFromTextFunction = parseHz;
            if (p) insertOutput.setValue(p->insertRate, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Dither");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertDither.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.").getDoubleValue();
            };
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel(fmtHzLabel("LPF", p ? (double)p->insertTone : 20000.0));
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvRate", (float)v); insertOutput.setLabel(fmtHzLabel("Rate", v)); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",  (float)v); insertTone.setLabel(fmtHzLabel("LPF",  v)); };
            break;

        case 5:
            insertDrive.setLabel("Threshold");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output (dB)");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel(fmtHzLabel("LPF", p ? (double)p->insertTone : 20000.0));
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); insertTone.setLabel(fmtHzLabel("LPF", v)); };
            break;

        case 6:
        {
            auto dbFmt = [](double v) -> juce::String {
                return juce::String(v, 1);
            };
            auto dbParse = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.-+").getDoubleValue();
            };

            insertDrive.setLabel("Low (dB)");
            insertDrive.setRange(-18.0, 18.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = dbFmt;
            insertDrive.getSlider().valueFromTextFunction = dbParse;
            if (p) insertDrive.setValue(p->insertDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("High (dB)");
            insertOutput.setRange(-18.0, 18.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = dbFmt;
            insertOutput.getSlider().valueFromTextFunction = dbParse;
            if (p) insertOutput.setValue(p->insertDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Mid (dB)");
            insertDither.setRange(-18.0, 18.0, 0.1);
            insertDither.getSlider().textFromValueFunction = dbFmt;
            insertDither.getSlider().valueFromTextFunction = dbParse;
            if (p) insertDither.setValue(p->insertEqMid, juce::dontSendNotification);
            else   insertDither.setValue(0.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel(fmtHzLabel("Mid", p ? juce::jlimit(200.0, 8000.0, (double)p->insertTone) : 1000.0));
            insertTone.getSlider().setSkewFactor(1.0);
            insertTone.setRange(200.0, 8000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(200.0, 8000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(1000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv",    (float)((v + 18.0) / 36.0 * 100.0)); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvDit",    (float)((v + 18.0) / 36.0 * 100.0)); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("eqMidGain", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",    (float)v); insertTone.setLabel(fmtHzLabel("Mid", v)); };
            break;
        }

        case 7: case 8:
        {
            insertDrive.setLabel(charId == 8 ? "Ceiling (dB)" : "Threshold (dB)");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4));
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(50.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output (dB)");
            insertOutput.setRange(-24.0, 24.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Attack (ms)");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v * 2.0));
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            else   insertDither.setValue(5.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            {
                const double toneInitVal = p ? juce::jlimit(20.0, 2000.0, (double)p->insertTone) : 100.0;
                insertTone.setLabel(toneInitVal < 1000.0 ? "Release (ms)" : "Release (s)");
                insertTone.setRange(20.0, 2000.0, 1.0);
                insertTone.getSlider().setSkewFactorFromMidPoint(200.0);
                insertTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                    return v < 1000.0 ? juce::String((int)v) : juce::String(v / 1000.0, 2);
                };
                insertTone.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                    auto t = s.trim().toLowerCase();
                    if (t.endsWith("ms")) return t.dropLastCharacters(2).trim().getDoubleValue();
                    if (t.endsWith("s"))  return t.dropLastCharacters(1).trim().getDoubleValue() * 1000.0;
                    return t.getDoubleValue();
                };
                insertTone.setValue(toneInitVal, juce::dontSendNotification);
                insertTone.setVisible(true);
            }

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); insertTone.setLabel(v < 1000.0 ? "Release (ms)" : "Release (s)"); };

            insertOutput.setGRSource(proc.getInsertGRReductionPtr(rhythmIndex));
            break;
        }

        case 9:
            insertDrive.setLabel("Mix");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(50.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setVisible(false);
            insertDither.setVisible(false);

            insertTone.setLabel(fmtHzLabel("Freq", p ? juce::jlimit(10.0, 5000.0, (double)p->insertTone) : 440.0));
            insertTone.getSlider().setSkewFactorFromMidPoint(223.6);
            insertTone.setRange(10.0, 5000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(10.0, 5000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(440.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive.onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertTone .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); insertTone.setLabel(fmtHzLabel("Freq", v)); };
            break;

        case 10:
            insertDrive.setLabel("Drive");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String((int)std::round(v)); };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output (dB)");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel(fmtHzLabel("Tone", p ? juce::jlimit(200.0, 20000.0, (double)p->insertTone) : 10000.0));
            insertTone.getSlider().setSkewFactor(1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(2000.0);
            insertTone.setRange(200.0, 20000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(200.0, 20000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(10000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); insertTone.setLabel(fmtHzLabel("Tone", v)); };
            break;

        case 11:
        {
            static const char* const kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

            insertDrive.setLabel("Note");
            insertDrive.setRange(0.0, 11.0, 1.0);
            insertDrive.getSlider().setSkewFactor(1.0);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 11, (int)std::round(v))];
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(juce::jlimit(0.0, 11.0, (double)p->insertDrive), juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Octave");
            insertOutput.setRange(0.0, 3.0, 1.0);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(juce::jlimit(0.0, 3.0, (double)p->insertBits), juce::dontSendNotification);
            else   insertOutput.setValue(1.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Feedback");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            else   insertDither.setValue(70.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel(fmtHzLabel("LPF", p ? juce::jlimit(20.0, 20000.0, (double)p->insertTone) : 20000.0));
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);
            insertTone.getSlider().textFromValueFunction = [](double v) { return fmtHzNum(v); };
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(20.0, 20000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(20000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv",  (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",  (float)v); insertTone.setLabel(fmtHzLabel("LPF", v)); };
            break;
        }

        case 12: case 13:
        {
            static const char* const kWaveNames[4] = { "Saw", "Square", "White", "Pink" };
            static const char* const kNoteNames[7] = { "C", "D", "E", "F", "G", "A", "B" };
            static const int kUnisonCounts[7] = { 1, 3, 5, 7, 9, 11, 13 };

            insertDrive.setLabel("Wave");
            insertDrive.setRange(0.0, 3.0, 1.0);
            insertDrive.getSlider().setSkewFactor(1.0);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kWaveNames[juce::jlimit(0, 3, (int)std::round(v))];
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(juce::jlimit(0.0, 3.0, (double)p->insertDrive), juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Unison");
            insertOutput.setRange(0.0, 6.0, 1.0);
            insertOutput.getSlider().setSkewFactor(1.0);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(kUnisonCounts[juce::jlimit(0, 6, (int)std::round(v))]);
            };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            {
                const double unisonKnob = p ? juce::jlimit(0.0, 6.0,
                                                ((double)p->insertOutput + 24.0) * 0.25)
                                            : 1.0;
                insertOutput.setValue(unisonKnob, juce::dontSendNotification);
            }
            insertOutput.setVisible(true);

            insertDither.setLabel("Octave");
            insertDither.setRange(1.0, 5.0, 1.0);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v));
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(juce::jlimit(1.0, 5.0, (double)p->insertDither), juce::dontSendNotification);
            else   insertDither.setValue(3.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel("Note");
            insertTone.getSlider().setSkewFactor(1.0);
            insertTone.setRange(1.0, 7.0, 1.0);
            insertTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 6, (int)std::round(v) - 1)];
            };
            insertTone.getSlider().valueFromTextFunction = nullptr;
            if (p) insertTone.setValue(juce::jlimit(1.0, 7.0, (double)p->insertBits), juce::dontSendNotification);
            else   insertTone.setValue(4.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            auto syncGreyOut = [this] {
                const int wave = juce::jlimit(0, 3, (int)std::round(insertDrive.getValue()));
                const bool pitched = (wave < 2);
                insertDither.setEnabled(pitched);  insertOutput.setEnabled(pitched);  insertTone.setEnabled(pitched);
                insertDither.setAlpha(pitched ? 1.0f : 0.45f);
                insertOutput.setAlpha(pitched ? 1.0f : 0.45f);
                insertTone  .setAlpha(pitched ? 1.0f : 0.45f);
            };
            syncGreyOut();

            insertDrive .onValueChanged = [this, syncGreyOut](double v) { apvtsSet("drvDrv", (float)v); syncGreyOut(); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut",  (float)(v * 4.0 - 24.0)); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            break;
        }

        default: break;
    }

    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
    { k->getSlider().updateText(); k->repaint(); }

    resized();

    if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
}

void InsertSubsection::resized()
{
    const int kW   = getWidth() / 4;
    const int gap  = 4;
    const int rowH = (getHeight() - gap) / 2;
    const int row2Y = rowH + gap;

    insertAlgo.setBounds(0, rowH / 4, 4 * kW, rowH / 2);

    const int selId = insertAlgo.getSelectedId();
    const bool showFour = (selId == 5 || (selId >= 7 && selId <= 9));
    insertDrive .setBounds(0 * kW, row2Y, kW, rowH);
    insertOutput.setBounds(1 * kW, row2Y, kW, rowH);
    if (showFour)
    {
        insertDither.setBounds(2 * kW, row2Y, kW, rowH);
        insertTone  .setBounds(3 * kW, row2Y, kW, rowH);
    }
    else
    {
        insertTone  .setBounds(2 * kW, row2Y, kW, rowH);
        insertDither.setBounds(3 * kW, row2Y, kW, rowH);
    }
}
