// Master-insert algorithm UI configuration.
//
// Stage 36: this was a ~480-line switch with one case per algorithm; the
// per-rhythm InsertSubsection carried a near-identical second copy of the
// same logic. Both now defer to the shared `mu_ui::configureKnobFromSlot`
// driver (Source/UI/InsertSlotUi.h), which reads label / range / skew /
// formatter from the per-algorithm config table in
// Source/Audio/InsertSlotConfig.h.
//
// The only master-specific extras left here are:
//   • Comp / Limiter GR meter — wired onto Param 2 (Output)
//   • Vocoder grey-out — Param 2/3/4 disable when Wave is noise

#include "MixerChannel.h"
#include "UI/InsertSlotUi.h"
#include "Audio/InsertSlotConfig.h"
#include "Plugin/ProcessorBase.h"
#include <cmath>

void MixerChannel::configureInsertAlgorithm(int charId, int slot, ProcessorBase* proc)
{
    if (!hasInsert()) return;

    KnobWithLabel* knobs[4] = {
        slot == 0 ? &insParam1   : &insParam1_2,
        slot == 0 ? &insParam2   : &insParam2_2,
        slot == 0 ? &insParam3   : &insParam3_2,
        slot == 0 ? &insParam4   : &insParam4_2,
    };

    // Reset per-slot state — GR meter source and grey-out — before the per-
    // algorithm config repopulates everything.
    for (auto* k : knobs)
    {
        k->onValueChanged = nullptr;
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }
    knobs[1]->setGRSource(nullptr);   // P2 carries the GR meter for comp/limiter

    // master insert may be reconfigured from loadFromAPVTS with proc=nullptr
    // (the dropdown value comes from APVTS, knob lambdas use the stored
    // masterInsertProc handle so the user can still drive the engine).
    ProcessorBase* const knobProc = masterInsertProc;

    const juce::String pChar = slot == 0 ? "mst_insChar" : "mst_ins2Char";
    const juce::String pSlot[4] = {
        slot == 0 ? juce::String("mst_insP1") : juce::String("mst_ins2P1"),
        slot == 0 ? juce::String("mst_insP2") : juce::String("mst_ins2P2"),
        slot == 0 ? juce::String("mst_insP3") : juce::String("mst_ins2P3"),
        slot == 0 ? juce::String("mst_insP4") : juce::String("mst_ins2P4"),
    };

    auto setParam = [knobProc](const juce::String& id, float normValue) {
        if (!knobProc) return;
        if (auto* p = knobProc->apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(normValue));
    };

    const VoiceParams& ip = masterInsertProc
        ? (slot == 0 ? masterInsertProc->mixerEngine.masterInsertParams
                     : masterInsertProc->mixerEngine.masterInsertParams2)
        : VoiceParams{};

    // None (algo 0) hides all four knobs via the config table (label==nullptr).
    if (charId == 0)
    {
        for (int s = 0; s < 4; ++s)
            mu_ui::configureKnobFromSlot(*knobs[s], 0, s, 0.0f, [](float){});
        if (proc) setParam(pChar, 0);
        return;
    }

    // Generic per-slot configuration.
    for (int s = 0; s < 4; ++s)
    {
        const float currentNorm = ip.insertParam[s];
        const juce::String paramId = pSlot[s];
        mu_ui::configureKnobFromSlot(*knobs[s], charId, s, currentNorm,
            [setParam, paramId](float newNorm) { setParam(paramId, newNorm); });
    }

    // Comp (7) / Limiter (8): wire GR meter onto P2 (Output).
    if ((charId == 7 || charId == 8) && masterInsertProc != nullptr)
    {
        knobs[1]->setGRSource(slot == 0
            ? &masterInsertProc->mixerEngine.masterInsert.grReduction
            : &masterInsertProc->mixerEngine.masterInsert2.grReduction);
    }

    // Vocoder / VocoderSt (12 / 13): P2/P3/P4 disable when Wave (P1) ≥ 2 (noise).
    if (charId == 12 || charId == 13)
    {
        auto syncGreyOut = [knobs]
        {
            const int wave = juce::jlimit(0, 3, (int) std::round(knobs[0]->getValue()));
            const bool pitched = (wave < 2);
            for (int i = 1; i <= 3; ++i)
            {
                knobs[i]->setEnabled(pitched);
                knobs[i]->setAlpha(pitched ? 1.0f : 0.45f);
            }
        };
        syncGreyOut();
        // Chain after the generic onValueChanged set by configureKnobFromSlot.
        const auto p1Id = pSlot[0];
        knobs[0]->onValueChanged = [knobs, p1Id, setParam, syncGreyOut, charId](double v)
        {
            setParam(p1Id, mu_ui::actualToNorm((float) v, charId, 0));
            syncGreyOut();
        };
    }

    if (proc) setParam(pChar, (float) charId);

    for (auto* k : knobs) { k->getSlider().updateText(); k->repaint(); }
    resized();
}
