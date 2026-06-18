#include "InsertSubsection.h"
#include "Plugin/ProcessorBase.h"
#include "Audio/InsertProcessor.h"
#include "Audio/InsertSlotConfig.h"
#include "UI/InsertSlotUi.h"

#include <algorithm>
#include <cmath>
#include <limits>

InsertSubsection::InsertSubsection(ProcessorBase& processor, juce::String channelPrefix)
    : proc(processor), prefix(std::move(channelPrefix))
{
    // Alphabetical UX order; dropdown id = canonical algo index + 1.
    insertAlgo.addItem("None",        1);    // algo 0
    insertAlgo.addItem("3-Band EQ",   7);    // algo 6
    insertAlgo.addItem("Bitcrusher",  5);    // algo 4
    insertAlgo.addItem("Clipper",     6);    // algo 5
    insertAlgo.addItem("Compressor",  8);    // algo 7
    insertAlgo.addItem("Fold",        4);    // algo 3
    insertAlgo.addItem("Hard Clip",   3);    // algo 2
    insertAlgo.addItem("Karplus",    12);    // algo 11
    insertAlgo.addItem("Limiter",     9);    // algo 8
    insertAlgo.addItem("Ring Mod",   10);    // algo 9
    insertAlgo.addItem("Soft Clip",   2);    // algo 1
    insertAlgo.addItem("Tape Sat",   11);    // algo 10
    insertAlgo.addItem("Vocoder",    13);    // algo 12
    insertAlgo.addItem("Vocoder St", 14);    // algo 13
    insertAlgo.setSelectedId(1, false);
    addAndMakeVisible(insertAlgo);

    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
        addAndMakeVisible(k);

    wireCallbacks();
}

juce::String InsertSubsection::paramFullId(const char* suffix) const
{
    return prefix + juce::String(channelIndex) + "_" + suffix;
}

bool InsertSubsection::validChannel() const
{
    return channelIndex >= 0 && channelIndex < proc.getNumChannels();
}

float InsertSubsection::readSlot(int slot) const
{
    const juce::String id = paramFullId(("insP" + juce::String(slot + 1)).toRawUTF8());
    if (auto* a = proc.apvts.getRawParameterValue(id)) return a->load();
    return 0.0f;
}

int InsertSubsection::currentAlgo() const
{
    if (auto* a = proc.apvts.getRawParameterValue(paramFullId("drvChar")))
        return (int) a->load();
    return 0;
}

void InsertSubsection::apvtsSet(const char* suffix, float v)
{
    if (channelIndex < 0) return;
    auto it = paramPtrCache.find(suffix);
    if (it == paramPtrCache.end())
        it = paramPtrCache.emplace(suffix, proc.apvts.getParameter(paramFullId(suffix))).first;
    if (auto* p = it->second)
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void InsertSubsection::wireCallbacks()
{
    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
        k->onStatusUpdate = [this](const juce::String& label, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate("Insert " + label, val);
        };

    insertAlgo.onChange = [this](int id)
    {
        const int newChar = id - 1;
        const int oldChar = validChannel() ? currentAlgo() : -1;

        // A/B snapshot: stash the OUTGOING algo's current ACTUAL slot values so
        // cycling back restores the user's edits instead of the defaults table.
        if (oldChar >= 0 && oldChar < mu_ui::kInsertAlgoCount && validChannel())
        {
            for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
                insertSnapshots[oldChar][slot] = mu_ui::normToActual(readSlot(slot), oldChar, slot);
            insertSnapshotValid[oldChar] = true;
        }

        // The 5-write algo switch. The product may wrap it (mu-clid: hot-swap
        // loading guard + resync) so its parameter listener doesn't fire
        // configureInsertAlgorithm with the stale algo mid-sequence.
        auto applyAlgoSwitch = [this, newChar]
        {
            const char* const slotSuffix[mu_ui::kInsertSlotCount] = { "insP1", "insP2", "insP3", "insP4" };
            for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
            {
                const float actual = insertSnapshotValid[newChar]
                    ? insertSnapshots[newChar][slot]
                    : mu_ui::kInsertAlgoDefaults[newChar][slot];
                apvtsSet(slotSuffix[slot], mu_ui::actualToNorm(actual, newChar, slot));
            }
            apvtsSet("drvChar", (float) newChar);
        };
        if (runBulkChange) runBulkChange(applyAlgoSwitch);
        else               applyAlgoSwitch();

        configureInsertAlgorithm(newChar);
        if (onStatusUpdate) onStatusUpdate("Insert Algorithm", insertAlgo.getText());
    };
}

void InsertSubsection::setChannel(int idx)
{
    if (idx != channelIndex)
        paramPtrCache.clear();
    channelIndex = idx;
    std::fill(std::begin(insertSnapshotValid), std::end(insertSnapshotValid), false);
    loadFromChannel();
    refreshModulatedIndicators();
    // Start the 30 Hz self-refresh when a channel is bound and mod hooks are wired.
    if (validChannel() && (isSlotModulated || slotModValue))
        startTimerHz(mu_ui::kUiRefreshHz);
    else
        stopTimer();
}

void InsertSubsection::loadFromChannel()
{
    if (! validChannel()) return;
    const int algo = currentAlgo();
    insertAlgo.setSelectedId(algo + 1, false);
    configureInsertAlgorithm(algo);
}

void InsertSubsection::refreshSuffix(const juce::String& suffix)
{
    if (! validChannel()) return;
    if (suffix == "drvChar"
     || suffix == "insP1" || suffix == "insP2" || suffix == "insP3" || suffix == "insP4")
    {
        if (suffix == "drvChar") insertAlgo.setSelectedId(currentAlgo() + 1, false);
        configureInsertAlgorithm(currentAlgo());
    }
}

void InsertSubsection::refreshModulatedIndicators()
{
    if (! validChannel()) return;
    const float kNaN   = std::numeric_limits<float>::quiet_NaN();
    const bool  playing = isPlaying ? isPlaying() : false;

    KnobWithLabel* knobs[mu_ui::kInsertSlotCount] = { &insertParam1, &insertParam2, &insertParam3, &insertParam4 };
    for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
    {
        const bool assigned = isSlotModulated ? isSlotModulated(slot) : false;
        knobs[slot]->setIsModulated(playing && assigned);
        knobs[slot]->setModulatedActual((assigned && playing && slotModValue) ? slotModValue(slot) : kNaN);
    }
}

void InsertSubsection::configureInsertAlgorithm(int charId)
{
    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
    {
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }
    insertParam2.setGRSource(nullptr);

    KnobWithLabel* knobs[mu_ui::kInsertSlotCount] = { &insertParam1, &insertParam2, &insertParam3, &insertParam4 };
    const char* const slotSuffix[mu_ui::kInsertSlotCount] = { "insP1", "insP2", "insP3", "insP4" };

    for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
    {
        const float currentNorm = validChannel() ? readSlot(slot) : 0.0f;
        const char* suffix = slotSuffix[slot];
        mu_ui::configureKnobFromSlot(*knobs[slot], charId, slot, currentNorm,
            [this, suffix](float newNorm) { apvtsSet(suffix, newNorm); });
    }

    // Compressor / Limiter expose a GR meter on P2.
    if (charId == 7 || charId == 8)
        insertParam2.setGRSource(getInsertGR ? getInsertGR() : nullptr);

    // Vocoder: when Wave (P1) is noise (≥2), P2/P3/P4 don't apply.
    if (charId == 12 || charId == 13)
    {
        auto syncGreyOut = [this]
        {
            const int  wave    = juce::jlimit(0, 3, (int) std::round(insertParam1.getValue()));
            const bool pitched = (wave < 2);
            for (auto* k : { &insertParam2, &insertParam3, &insertParam4 })
            {
                k->setEnabled(pitched);
                k->setAlpha(pitched ? 1.0f : 0.45f);
            }
        };
        syncGreyOut();
        insertParam1.onValueChanged = [this, syncGreyOut](double v) {
            apvtsSet("insP1", mu_ui::actualToNorm((float) v, currentAlgo(), 0));
            syncGreyOut();
        };
    }

    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
    { k->getSlider().updateText(); k->repaint(); }

    resized();

    if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
}

void InsertSubsection::resized()
{
    using LF = MuLookAndFeel;
    using mu_ui::s;
    constexpr int kW   = LF::kKnobSize2W;
    constexpr int rowH = LF::kKnobSize2H;
    constexpr int gap  = LF::kVoiceGap;
    const int row2Y = rowH + gap;

    insertAlgo.setBounds(0, s(rowH / 4), s(4 * kW), s(rowH / 2));
    insertParam1.setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    insertParam2.setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    insertParam3.setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    insertParam4.setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
}
