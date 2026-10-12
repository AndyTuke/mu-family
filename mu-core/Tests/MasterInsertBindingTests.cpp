#include "UI/MasterInsertBinding.h"
#include "Plugin/MixerFxParams.h"
#include "TestProcessorBase.h"

// The plugin half of the master insert column: MasterInsertPanel <-> mst_ins* / mst_ins2* parameters,
// on a processor that carries the shared mixer parameter layout.
namespace
{
struct MixerParamProcessor : TestProcessorBase
{
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        mu_mixfx::addGlobalFxParams(layout);
        return layout;
    }
    MixerParamProcessor() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout()) { startFxParamSync(); }
};

float plain(MixerParamProcessor& p, const char* id)
{
    return p.apvts.getParameter(id)->convertFrom0to1(p.apvts.getParameter(id)->getValue());
}
} // namespace

struct MasterInsertBindingTests : juce::UnitTest
{
    MasterInsertBindingTests() : juce::UnitTest("Master insert binding", "mu-core") {}

    void runTest() override
    {
        using mu_ui::MasterInsertBinding;

        beginTest("parameter ids follow the mst_ins / mst_ins2 scheme");
        {
            expectEquals(juce::String(MasterInsertBinding::algorithmParamId(0)), juce::String("mst_insChar"));
            expectEquals(juce::String(MasterInsertBinding::algorithmParamId(1)), juce::String("mst_ins2Char"));
            expectEquals(juce::String(MasterInsertBinding::paramId(0, 0)), juce::String("mst_insP1"));
            expectEquals(juce::String(MasterInsertBinding::paramId(1, 3)), juce::String("mst_ins2P4"));
            MixerParamProcessor p;
            for (int slot = 0; slot < 2; ++slot)
            {
                expect(p.apvts.getParameter(MasterInsertBinding::algorithmParamId(slot)) != nullptr);
                for (int i = 0; i < 4; ++i)
                    expect(p.apvts.getParameter(MasterInsertBinding::paramId(slot, i)) != nullptr);
            }
        }

        beginTest("a panel change writes the parameters, the algorithm and the four values");
        {
            MixerParamProcessor p;
            mu_ui::MasterInsertPanel panel;
            MasterInsertBinding binding(p, panel);

            panel.getAlgorithmSelect(0).getComboBox().setSelectedId(7 + 1, juce::sendNotificationSync);   // Compressor
            expectEquals((int) plain(p, "mst_insChar"), 7);
            expectEquals(p.mixerEngine.getMasterInsertParams(0).insertAlgo, 7);
            for (int i = 0; i < 4; ++i)
                expectWithinAbsoluteError(plain(p, MasterInsertBinding::paramId(0, i)), panel.getParam(0, i), 0.001f);

            panel.getAlgorithmSelect(1).getComboBox().setSelectedId(6 + 1, juce::sendNotificationSync);   // EQ, second insert
            expectEquals((int) plain(p, "mst_ins2Char"), 6);
            expectEquals((int) plain(p, "mst_insChar"), 7);                 // slot 1 untouched

            // A knob move writes only its parameter, as the stored normalised value.
            auto& slider = panel.getKnob(1, 2).getSlider();
            slider.setValue(slider.getMaximum(), juce::sendNotificationSync);
            expectWithinAbsoluteError(plain(p, "mst_ins2P3"), 1.0f, 0.001f);
            expectWithinAbsoluteError(p.mixerEngine.getMasterInsertParams(1).insertParam[2], 1.0f, 0.001f);
        }

        beginTest("refresh pushes the engine state into the panel and round-trips");
        {
            MixerParamProcessor p;
            mu_ui::MasterInsertPanel panel;
            MasterInsertBinding binding(p, panel);

            // The host (automation, a preset) sets the parameters behind the panel's back.
            auto set = [&](const char* id, float plainValue)
            {
                auto* prm = p.apvts.getParameter(id);
                prm->setValueNotifyingHost(prm->convertTo0to1(plainValue));
            };
            set("mst_insChar", 6.0f);
            set("mst_insP1", 0.25f);
            set("mst_insP2", 0.5f);
            set("mst_ins2Char", 12.0f);
            set("mst_ins2P1", 0.75f);
            expectEquals(panel.getAlgorithm(0), 0);                  // not yet: refresh is the flush

            binding.refresh();
            expectEquals(panel.getAlgorithm(0), 6);
            expectEquals(panel.getAlgorithm(1), 12);
            expectWithinAbsoluteError(panel.getParam(0, 0), 0.25f, 0.001f);
            expectWithinAbsoluteError(panel.getParam(0, 1), 0.5f, 0.001f);
            expectWithinAbsoluteError(panel.getParam(1, 0), 0.75f, 0.001f);
            expectEquals(panel.getAlgorithmSelect(0).getComboBox().getSelectedId(), 6 + 1);

            // Refreshing does not write anything back (the engine values stay as they were set).
            expectEquals((int) plain(p, "mst_insChar"), 6);
            expectWithinAbsoluteError(plain(p, "mst_insP2"), 0.5f, 0.001f);

            // The panel's knobs show the algorithm's actual values for those norms.
            expectWithinAbsoluteError((float) panel.getKnob(0, 1).getSlider().getValue(),
                                      mu_ui::normToActual(0.5f, 6, 1), 0.01f);
        }

        beginTest("refresh greys out the Vocoder's P2 to P4 from the engine's Wave value");
        {
            MixerParamProcessor p;
            mu_ui::MasterInsertPanel panel;
            MasterInsertBinding binding(p, panel);
            auto set = [&](const char* id, float plainValue)
            {
                auto* prm = p.apvts.getParameter(id);
                prm->setValueNotifyingHost(prm->convertTo0to1(plainValue));
            };
            set("mst_insChar", 12.0f);
            set("mst_insP1", mu_ui::actualToNorm(3.0f, 12, 0));       // Pink noise
            binding.refresh();
            for (int i = 1; i < 4; ++i)
                expect(! panel.getKnob(0, i).isEnabled(), "noise wave greys the pitch knobs");

            set("mst_insP1", mu_ui::actualToNorm(0.0f, 12, 0));       // back to Saw
            binding.refresh();
            for (int i = 1; i < 4; ++i)
                expect(panel.getKnob(0, i).isEnabled(), "a pitched wave enables them again");
        }

        beginTest("the binding detaches its callbacks when destroyed");
        {
            MixerParamProcessor p;
            mu_ui::MasterInsertPanel panel;
            {
                MasterInsertBinding binding(p, panel);
                expect(panel.onAlgorithm != nullptr && panel.onParam != nullptr);
            }
            expect(panel.onAlgorithm == nullptr && panel.onParam == nullptr);
            panel.getAlgorithmSelect(0).getComboBox().setSelectedId(3, juce::sendNotificationSync);   // no crash
        }
    }
};

static MasterInsertBindingTests masterInsertBindingTests;
