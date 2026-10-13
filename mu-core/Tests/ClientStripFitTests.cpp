#include "UI/MixerChannel.h"
#include "UI/InsertSlotUi.h"

#include <vector>

// Layout and behaviour of the mixer Client strip (design-ui mu-link section 3): nothing
// overlaps or leaves the strip at any display size, the fader is not capped, the EQ stack runs
// High / Mid Hz / Mid / Low, and an inactive (empty) slot still works.
namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// The visible, non-label children of the strip.
std::vector<juce::Component*> blocks(MixerChannel& strip)
{
    std::vector<juce::Component*> out;
    for (auto* c : strip.getChildren())
        if (c->isVisible() && dynamic_cast<juce::Label*>(c) == nullptr)
            out.push_back(c);
    return out;
}

juce::Slider* findFader(MixerChannel& strip)
{
    for (auto* c : strip.getChildren())
        if (auto* sl = dynamic_cast<juce::Slider*>(c)) return sl;
    return nullptr;
}
} // namespace

struct ClientStripFitTests : juce::UnitTest
{
    ClientStripFitTests() : juce::UnitTest("MixerChannel client strip", "mu-core") {}

    // Strip size from the mu-link mixer panel (design-ui section 3): 8 strips plus the master
    // across the panel; height = the 662 px mixer panel less the channel inset, header and plate gap.
    static juce::Rectangle<int> stripBounds()
    {
        using LF = MuLookAndFeel;
        const int w = (LF::kWindowWidth - 2 * LF::kScrewedPanelInset - LF::kMixerMasterTotalW - LF::kMixerDivW
                       - 7 * LF::kMixerChanGap) / 8;
        const int h = 662 - 2 * LF::kChannelInset - LF::kMixerHeaderH - 4;
        return { 0, 0, mu_ui::s(w), mu_ui::s(h) };
    }

    void checkFit(float scale)
    {
        ScaleGuard guard;
        mu_ui::scale = scale;
        const juce::String tag = "scale " + juce::String(scale, 2) + ": ";

        MixerChannel strip(MixerChannel::Type::Client, "Synth", MuLookAndFeel::layerPalette[2]);
        strip.setBounds(stripBounds());

        const auto bounds = strip.getLocalBounds();
        const auto items = blocks(strip);
        int knobs = 0;
        for (auto* c : items)
        {
            expect(bounds.contains(c->getBounds()), tag + "child inside the strip");
            if (dynamic_cast<KnobWithLabel*>(c) != nullptr) ++knobs;
        }
        expectEquals(knobs, 4);                            // the EQ stack only: no sends, pan or sidechain
        expectEquals((int) items.size(), 4 + 2 + 2 + 1);   // + fader and VU, mute and solo, the Dropouts lamp; no combo boxes

        // No two blocks overlap.
        for (size_t i = 0; i < items.size(); ++i)
            for (size_t j = i + 1; j < items.size(); ++j)
                expect(! items[i]->getBounds().intersects(items[j]->getBounds()),
                       tag + "blocks " + juce::String((int) i) + " and " + juce::String((int) j) + " overlap");

        // The EQ stack runs top to bottom High, Mid Hz, Mid, Low, each at the strip-knob size.
        using Eq = MixerChannel::ClientEq;
        int lastBottom = 0;
        for (auto band : { Eq::High, Eq::MidHz, Eq::Mid, Eq::Low })
        {
            auto& k = strip.getClientEqKnob(band);
            expect(k.getY() >= lastBottom, tag + "EQ knobs stack in order");
            expectEquals(k.getHeight(), mu_ui::s(MuLookAndFeel::kMixerStripKnobH));
            lastBottom = k.getBottom();
        }

        // The Dropouts lamp is the bottom row, under mute / solo, inside the strip.
        {
            auto& lamp = strip.getDropoutLamp();
            expect(lamp.isVisible() && lamp.getBottom() <= bounds.getBottom(), tag + "lamp inside the strip");
            for (auto* c : items)
                if (auto* b = dynamic_cast<juce::TextButton*>(c))
                    expect(lamp.getY() >= b->getBottom(), tag + "the lamp sits under mute / solo");
        }

        // The fader takes all the height left: far above the 200 px cap other strips use, never under 40.
        auto* fader = findFader(strip);
        expect(fader != nullptr, tag + "fader found");
        if (fader != nullptr)
        {
            expect(fader->getHeight() > mu_ui::s(200), tag + "fader is uncapped");
            expect(fader->getHeight() >= mu_ui::s(MuLookAndFeel::kClientFaderMinH), tag + "fader above its minimum");
            expect(fader->getY() >= lastBottom, tag + "fader sits under the EQ stack");
        }
    }

    void runTest() override
    {
        beginTest("client strip fits at Small, Medium and Large");
        checkFit(0.85f);
        checkFit(1.0f);
        checkFit(1.15f);

        beginTest("EQ knobs carry the shared EQ table ranges");
        {
            MixerChannel strip(MixerChannel::Type::Client, "", juce::Colours::red);
            using Eq = MixerChannel::ClientEq;
            auto range = [&](Eq b) { return strip.getClientEqKnob(b).getSlider().getRange(); };
            expectEquals(range(Eq::High).getStart(), -18.0);
            expectEquals(range(Eq::High).getEnd(), 18.0);
            expectEquals(range(Eq::Low).getStart(), -18.0);
            expectEquals(range(Eq::Mid).getEnd(), 18.0);
            expectEquals(range(Eq::MidHz).getStart(), 200.0);
            expectEquals(range(Eq::MidHz).getEnd(), 8000.0);
            expectEquals(strip.getClientEqKnob(Eq::MidHz).getValue(), 1000.0);   // flat default
            expectEquals(strip.getClientEqKnob(Eq::High).getValue(), 0.0);
        }

        beginTest("EQ labels carry no units and never change");
        {
            MixerChannel strip(MixerChannel::Type::Client, "", juce::Colours::red);
            using Eq = MixerChannel::ClientEq;
            const std::pair<Eq, const char*> expected[] = { { Eq::High, "High" }, { Eq::MidHz, "Mid Hz" },
                                                            { Eq::Mid, "Mid" }, { Eq::Low, "Low" } };
            for (auto& [band, text] : expected)
                expectEquals(strip.getClientEqKnob(band).getLabel(), juce::String(text));

            strip.getClientEqKnob(Eq::MidHz).getSlider().setValue(4000.0, juce::sendNotificationSync);
            strip.getClientEqKnob(Eq::High).getSlider().setValue(6.0, juce::sendNotificationSync);
            for (auto& [band, text] : expected)
                expectEquals(strip.getClientEqKnob(band).getLabel(), juce::String(text));
        }

        beginTest("a dynamic-unit slot label stays put across 1000, and Suffix still flips it");
        {
            constexpr int kSoftClip = 1, kLpf = 3;   // LPF is a dynamicUnit Hz slot
            KnobWithLabel plain("", MuLookAndFeel::knobInsertPad), suffixed("", MuLookAndFeel::knobInsertPad);
            mu_ui::configureKnobFromSlot(plain, kSoftClip, kLpf, 0.2f, [](float) {});
            mu_ui::configureKnobFromSlot(suffixed, kSoftClip, kLpf, 0.2f, [](float) {},
                                         mu_ui::SlotLabelUnits::Suffix);
            expectEquals(plain.getLabel(), juce::String("LPF"));
            plain.getSlider().setValue(500.0, juce::sendNotificationSync);
            plain.getSlider().setValue(5000.0, juce::sendNotificationSync);
            expectEquals(plain.getLabel(), juce::String("LPF"));

            suffixed.getSlider().setValue(500.0, juce::sendNotificationSync);
            expectEquals(suffixed.getLabel(), juce::String("LPF (Hz)"));
            suffixed.getSlider().setValue(5000.0, juce::sendNotificationSync);
            expectEquals(suffixed.getLabel(), juce::String("LPF (kHz)"));
        }

        beginTest("controls call back with the right band, level, mute and solo");
        {
            MixerChannel strip(MixerChannel::Type::Client, "App", juce::Colours::red);
            using Eq = MixerChannel::ClientEq;
            int eqFired = 0;
            Eq lastBand = Eq::Low;
            float lastActual = 0.0f, level = -1.0f;
            int muteCalls = 0, soloCalls = 0;
            strip.onClientEq = [&](Eq b, float v) { ++eqFired; lastBand = b; lastActual = v; };
            strip.onClientLevel = [&](float v) { level = v; };
            strip.onClientMute = [&](bool) { ++muteCalls; };
            strip.onClientSolo = [&](bool) { ++soloCalls; };

            strip.getClientEqKnob(Eq::Mid).getSlider().setValue(6.0, juce::sendNotificationSync);
            expectEquals(eqFired, 1);
            expect(lastBand == Eq::Mid);
            expectWithinAbsoluteError(lastActual, 6.0f, 0.01f);

            // The setters push state in without calling back.
            strip.setClientEq(Eq::High, -3.0f);
            strip.setClientLevel(0.5f);
            strip.setClientMuteSolo(true, true);
            expectEquals(eqFired, 1);
            expectEquals(level, -1.0f);
            expectEquals(muteCalls + soloCalls, 0);

            auto* fader = findFader(strip);
            expect(fader != nullptr);
            if (fader != nullptr)
            {
                expectWithinAbsoluteError((float) fader->getValue(), 0.5f, 0.001f);
                fader->setValue(1.5, juce::sendNotificationSync);
                expectWithinAbsoluteError(level, 1.5f, 0.001f);
            }

            for (auto* c : strip.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*>(c))
                    b->onClick();
            expectEquals(muteCalls, 1);
            expectEquals(soloCalls, 1);
        }

        beginTest("an empty slot is dimmed but stays operable");
        {
            MixerChannel strip(MixerChannel::Type::Client, "", juce::Colours::red);
            strip.setActive(false);
            for (auto* c : strip.getChildren())
                expect(c->isEnabled(), "control still enabled while the slot is empty");
        }

        beginTest("dB readout: one decimal, signed above unity, no unit");
        {
            expectEquals(MixerChannel::formatClientDb(1.0f), juce::String("0.0"));
            expectEquals(MixerChannel::formatClientDb(0.5f), juce::String("-6.0"));
            expectEquals(MixerChannel::formatClientDb(1.5f), juce::String("+3.5"));
            expectEquals(MixerChannel::formatClientDb(0.0f), juce::String("-inf"));
        }
    }
};

static ClientStripFitTests clientStripFitTests;
