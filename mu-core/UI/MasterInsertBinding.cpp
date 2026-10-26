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

// ── Group bus inserts ─────────────────────────────────────────────────────────────────────────────
static_assert(mu_limits::kMaxBuses == 0 || mu_limits::kBusInserts <= MasterInsertPanel::kSlots,
              "a bus insert column shows at most the Master panel's boxes: a longer chain needs a taller panel");

int BusInsertBinding::numSlots() noexcept
{
    return juce::jlimit(1, MasterInsertPanel::kSlots, mu_limits::kBusInserts);
}

juce::String BusInsertBinding::algorithmParamId(int bus, int insert)
{
    return "bus" + juce::String(bus) + "_i" + juce::String(insert) + "_drvChar";
}

juce::String BusInsertBinding::paramId(int bus, int insert, int param)
{
    return "bus" + juce::String(bus) + "_i" + juce::String(insert) + "_insP" + juce::String(param + 1);
}

BusInsertBinding::BusInsertBinding(ProcessorBase& processor, MasterInsertPanel& insertPanel, int busIndex)
    : proc(processor), panel(insertPanel), bus(busIndex)
{
    panel.setNumSlots(numSlots());
    panel.onAlgorithm = [this](int slot, int algorithm) { write(algorithmParamId(bus, slot), (float) algorithm); };
    panel.onParam     = [this](int slot, int param, float norm) { write(paramId(bus, slot, param), norm); };

    // The Compressor / Limiter gain-reduction meters read the bus's own inserts.
    if (bus >= 0 && bus < mu_limits::kMaxBuses)
        for (int slot = 0; slot < numSlots(); ++slot)
            panel.setGainReductionSource(slot, &proc.mixerEngine.buses[(size_t) bus].insert[slot].processor.grReduction);

    refresh();
}

BusInsertBinding::~BusInsertBinding()
{
    panel.onAlgorithm = nullptr;
    panel.onParam     = nullptr;
    for (int slot = 0; slot < numSlots(); ++slot)
        panel.setGainReductionSource(slot, nullptr);
}

void BusInsertBinding::write(const juce::String& parameterId, float plainValue)
{
    if (auto* p = proc.apvts.getParameter(parameterId))
        p->setValueNotifyingHost(p->convertTo0to1(plainValue));
}

void BusInsertBinding::refresh()
{
    if (bus < 0 || bus >= mu_limits::kMaxBuses) return;
    for (int slot = 0; slot < numSlots(); ++slot)
    {
        const VoiceParams ip = proc.mixerEngine.buses[(size_t) bus].insert[slot].snapshot();

        // As the master column: reconfigure the knobs only when the algorithm differs.
        const int algorithm = juce::jlimit(0, kInsertAlgoCount - 1, ip.insertAlgo);
        if (panel.getAlgorithm(slot) != algorithm)
            panel.setAlgorithm(slot, algorithm);
        for (int p = 0; p < MasterInsertPanel::kParams; ++p)
            panel.setParam(slot, p, ip.insertParam[p]);
    }
}
} // namespace mu_ui
