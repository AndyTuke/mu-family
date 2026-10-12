#include "UI/MasterInsertBinding.h"
#include "Plugin/ProcessorBase.h"

namespace mu_ui
{
namespace
{
constexpr const char* kAlgoIds[MasterInsertPanel::kSlots] = { "mst_insChar", "mst_ins2Char" };
constexpr const char* kParamIds[MasterInsertPanel::kSlots][MasterInsertPanel::kParams] = {
    { "mst_insP1",  "mst_insP2",  "mst_insP3",  "mst_insP4"  },
    { "mst_ins2P1", "mst_ins2P2", "mst_ins2P3", "mst_ins2P4" } };
} // namespace

const char* MasterInsertBinding::algorithmParamId(int slot) noexcept
{
    return kAlgoIds[juce::jlimit(0, MasterInsertPanel::kSlots - 1, slot)];
}

const char* MasterInsertBinding::paramId(int slot, int param) noexcept
{
    return kParamIds[juce::jlimit(0, MasterInsertPanel::kSlots - 1, slot)]
                    [juce::jlimit(0, MasterInsertPanel::kParams - 1, param)];
}

MasterInsertBinding::MasterInsertBinding(ProcessorBase& processor, MasterInsertPanel& insertPanel)
    : proc(processor), panel(insertPanel)
{
    // A new algorithm: the panel reports it first, then each of the four parameters it restored.
    panel.onAlgorithm = [this](int slot, int algorithm) { write(algorithmParamId(slot), (float) algorithm); };
    panel.onParam     = [this](int slot, int param, float norm) { write(paramId(slot, param), norm); };

    // The Compressor / Limiter gain-reduction meters read the engine's inserts.
    panel.setGainReductionSource(0, &proc.mixerEngine.masterInsert .grReduction);
    panel.setGainReductionSource(1, &proc.mixerEngine.masterInsert2.grReduction);

    refresh();
}

MasterInsertBinding::~MasterInsertBinding()
{
    panel.onAlgorithm = nullptr;
    panel.onParam     = nullptr;
    panel.setGainReductionSource(0, nullptr);
    panel.setGainReductionSource(1, nullptr);
}

void MasterInsertBinding::write(const char* parameterId, float plainValue)
{
    if (auto* p = proc.apvts.getParameter(parameterId))
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
}

void MasterInsertBinding::refresh()
{
    for (int slot = 0; slot < MasterInsertPanel::kSlots; ++slot)
    {
        const VoiceParams ip = proc.mixerEngine.getMasterInsertParams(slot);

        // Reconfigure the knobs only when the algorithm differs, so a steady refresh never
        // restarts a knob that is being dragged.
        const int algorithm = juce::jlimit(0, kInsertAlgoCount - 1, ip.insertAlgo);
        if (panel.getAlgorithm(slot) != algorithm)
            panel.setAlgorithm(slot, algorithm);

        for (int p = 0; p < MasterInsertPanel::kParams; ++p)
            panel.setParam(slot, p, ip.insertParam[p]);
    }
}
} // namespace mu_ui
