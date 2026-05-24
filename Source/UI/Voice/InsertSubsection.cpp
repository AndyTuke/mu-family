#include "InsertSubsection.h"
#include "Plugin/PluginProcessor.h"
#include "Audio/InsertProcessor.h"
#include "Audio/InsertSlotConfig.h"
#include "Modulation/ModulationSnapshot.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "Sequencer/Rhythm.h"
#include "../InsertSlotUi.h"

InsertSubsection::InsertSubsection(PluginProcessor& p) : proc(p)
{
    // Dropdown IDs are 1-based; map ID = algoIdx + 1. The kInsertAlgorithmNames
    // table in Source/Audio/AlgorithmNames.h owns the canonical ordering;
    // the dropdown rearranges for alphabetical UX presentation only.
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

void InsertSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    auto it = paramPtrCache.find(suffix);
    if (it == paramPtrCache.end())
    {
        const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
        it = paramPtrCache.emplace(suffix, proc.apvts.getParameter(id)).first;
    }
    if (auto* p = it->second)
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void InsertSubsection::wireCallbacks()
{
    // Status-bar forwarding for the 4 slot knobs. The label string captured
    // here is whatever configureKnobFromSlot most recently set — picks up the
    // per-algorithm name automatically (e.g. "Drive" for SoftClip, "Bits"
    // for Bitcrusher, "Note" for Karplus).
    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
    {
        k->onStatusUpdate = [this, k](const juce::String& label, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate("Insert " + label, val);
        };
    }

    insertAlgo.onChange = [this](int id) {
        const int newChar = id - 1;
        const int oldChar = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                            ? proc.getRhythm(rhythmIndex).voiceParams.insertAlgo : -1;

        // A/B-style snapshot: stash the OUTGOING algorithm's current slot
        // values (as ACTUAL, denormalised) so cycling back restores the user's
        // edits instead of reverting to the defaults table.
        if (oldChar >= 0 && oldChar < InsertProcessor::kNumInsertAlgos
            && rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
        {
            const auto& vp = proc.getRhythm(rhythmIndex).voiceParams;
            for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
                insertSnapshots[oldChar][slot] = mu_ui::normToActual(vp.insertParam[slot], oldChar, slot);
            insertSnapshotValid[oldChar] = true;
        }

        // Wrap the 5-write algorithm switch in apvtsLoading so RhythmPanel's
        // parameterChanged listener skips its inline refreshSuffix calls —
        // otherwise each insP{1..4} write triggers configureInsertAlgorithm
        // with the STALE algo (drvChar hasn't been set yet) and the old
        // algo's per-slot config table fires `setVisible(false)` on hidden
        // slots, leaving the knobs hidden after the sequence settles.
        //
        // Engine sync (voiceEngines[ri]->setParams) is also suppressed under
        // the guard, so we manually re-sync via forceSyncRhythmFromAPVTS
        // after the guard exits — same pattern as preset load.
        {
            mu_core::ScopedApvtsLoading guard(proc.getApvtsLoadingFlag());
            const char* const slotSuffix[4] = { "insP1", "insP2", "insP3", "insP4" };
            for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
            {
                const float actual = insertSnapshotValid[newChar]
                    ? insertSnapshots[newChar][slot]
                    : mu_ui::kInsertAlgoDefaults[newChar][slot];
                apvtsSet(slotSuffix[slot], mu_ui::actualToNorm(actual, newChar, slot));
            }
            apvtsSet("drvChar", (float) newChar);
        }
        if (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
            proc.forceSyncRhythmFromAPVTS(rhythmIndex);

        configureInsertAlgorithm(newChar);
        if (onStatusUpdate) onStatusUpdate("Insert Algorithm", insertAlgo.getText());
    };
}

void InsertSubsection::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();
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
     || suffix == "insP1" || suffix == "insP2" || suffix == "insP3" || suffix == "insP4")
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

    KnobWithLabel* knobs[4] = { &insertParam1, &insertParam2, &insertParam3, &insertParam4 };
    const char* destIds[4]  = { "insert.p1", "insert.p2", "insert.p3", "insert.p4" };
    const int   snapIdx[4]  = { kSnapInsP1, kSnapInsP2, kSnapInsP3, kSnapInsP4 };

    for (int slot = 0; slot < 4; ++slot)
    {
        const bool assigned = isAssigned(destIds[slot]);
        knobs[slot]->setIsModulated(playing && assigned);
        // Snapshot now stores the actual (denormalised) slot value — route
        // via setModulatedActual so the arc proportion matches the slider's
        // visual scale (Linear / Log / IntStep per the active algo).
        knobs[slot]->setModulatedActual((assigned && playing) ? sn(snapIdx[slot]) : kNaN);
    }
}

void InsertSubsection::configureInsertAlgorithm(int charId)
{
    // Reset any per-algo grey-out before the per-slot config repopulates state.
    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
    {
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }
    insertParam2.setGRSource(nullptr);   // re-set below if comp/limiter

    const VoiceParams* vp = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                              ? &proc.getRhythm(rhythmIndex).voiceParams : nullptr;

    KnobWithLabel* knobs[4] = { &insertParam1, &insertParam2, &insertParam3, &insertParam4 };
    const char* const slotSuffix[4] = { "insP1", "insP2", "insP3", "insP4" };

    for (int slot = 0; slot < 4; ++slot)
    {
        const float currentNorm = vp ? vp->insertParam[slot] : 0.0f;
        const char* suffix = slotSuffix[slot];
        mu_ui::configureKnobFromSlot(*knobs[slot], charId, slot, currentNorm,
            [this, suffix](float newNorm) { apvtsSet(suffix, newNorm); });
    }

    // Algorithm-specific extras: Compressor / Limiter expose GR meter on P2.
    if (charId == 7 || charId == 8)
        insertParam2.setGRSource(proc.getInsertGRReductionPtr(rhythmIndex));

    // Vocoder grey-out: when Wave (P1) is set to noise (≥2), P2/P3/P4 don't
    // apply. Hook the wave knob's onValueChanged to re-evaluate.
    if (charId == 12 || charId == 13)
    {
        auto syncGreyOut = [this]
        {
            const int wave = juce::jlimit(0, 3, (int) std::round(insertParam1.getValue()));
            const bool pitched = (wave < 2);
            for (auto* k : { &insertParam2, &insertParam3, &insertParam4 })
            {
                k->setEnabled(pitched);
                k->setAlpha(pitched ? 1.0f : 0.45f);
            }
        };
        syncGreyOut();
        const auto suffixP1 = slotSuffix[0];
        insertParam1.onValueChanged = [this, suffixP1, syncGreyOut](double v) {
            apvtsSet(suffixP1, mu_ui::actualToNorm((float) v,
                proc.getRhythm(rhythmIndex).voiceParams.insertAlgo, 0));
            syncGreyOut();
            // Match the dynamicUnit label-refresh side effect from configureKnobFromSlot;
            // not strictly needed for Vocoder (enum names, no unit) but keeps the
            // path uniform.
        };
    }

    for (auto* k : { &insertParam1, &insertParam2, &insertParam3, &insertParam4 })
    { k->getSlider().updateText(); k->repaint(); }

    resized();

    if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
}

void InsertSubsection::resized()
{
    // Voice section knobs render at Size 2 (55 × 56) — fixed PX, no
    // dependency on the panel's actual height.
    constexpr int kW    = MuClidLookAndFeel::kKnobSize2W;
    constexpr int rowH  = MuClidLookAndFeel::kKnobSize2H;
    constexpr int gap   = MuClidLookAndFeel::kVoiceGap;
    constexpr int row2Y = rowH + gap;

    using mu_ui::s;
    insertAlgo.setBounds(0, s(rowH / 4), s(4 * kW), s(rowH / 2));

    insertParam1.setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    insertParam2.setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    insertParam3.setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    insertParam4.setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
}
