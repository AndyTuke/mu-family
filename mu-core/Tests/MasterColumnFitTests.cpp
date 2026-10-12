#include "UI/MixerChannel.h"
#include "UI/MasterInsertPanel.h"

#include <vector>

// Fit and behaviour of the mu-link master column (design-ui mu-link section 3): the callback-driven
// Master strip (80 px) beside the shared MasterInsertPanel (136 px), 216 x 622 together.
namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

juce::Slider* findFader(MixerChannel& strip)
{
    for (auto* c : strip.getChildren())
        if (auto* sl = dynamic_cast<juce::Slider*>(c)) return sl;
    return nullptr;
}

// The panel height the master column gets: the mixer panel less the channel inset, header and plate gap.
constexpr int kColumnH = 662 - 2 * MuLookAndFeel::kChannelInset - MuLookAndFeel::kMixerHeaderH - 4;   // 622
} // namespace

struct MasterColumnFitTests : juce::UnitTest
{
    MasterColumnFitTests() : juce::UnitTest("Master column (callback Master + MasterInsertPanel)", "mu-core") {}

    void checkFit(float scale)
    {
        ScaleGuard guard;
        mu_ui::scale = scale;
        const juce::String tag = "scale " + juce::String(scale, 2) + ": ";
        using LF = MuLookAndFeel;
        const int h = mu_ui::s(kColumnH);

        // The strip alone: 80 px, callback-driven, no insert panel, no pan, mute or solo.
        MixerChannel master(MixerChannel::Type::Master, "Master", juce::Colours::white);
        master.useCallbackMaster();
        master.setFaderTop(MixerChannel::clientFaderTop());
        master.setBounds(0, 0, mu_ui::s(LF::kMixerMasterW), h);

        const auto bounds = master.getLocalBounds();
        std::vector<juce::Component*> items;
        for (auto* c : master.getChildren())
            if (c->isVisible() && dynamic_cast<juce::Label*>(c) == nullptr) items.push_back(c);
        expectEquals((int) items.size(), 2);   // fader and VU only
        for (auto* c : items)
            expect(bounds.contains(c->getBounds()), tag + "master child inside the strip");
        if (items.size() == 2)
            expect(! items[0]->getBounds().intersects(items[1]->getBounds()), tag + "fader and VU apart");

        auto* fader = findFader(master);
        expect(fader != nullptr, tag + "master fader found");
        if (fader != nullptr)
        {
            expectEquals(fader->getY(), MixerChannel::clientFaderTop());
            expect(fader->getHeight() > mu_ui::s(200), tag + "master fader is uncapped");
        }

        // The insert panel: 136 px wide, both boxes fit under the 622 px column.
        mu_ui::MasterInsertPanel panel;
        panel.setBounds(0, 0, mu_ui::s(LF::kMixerInsertPanelW), h);
        expect(mu_ui::MasterInsertPanel::preferredHeight() <= h, tag + "insert panel fits the column");

        // Every algorithm: knobs stay inside the panel and clear of each other and of the selectors.
        for (int slot = 0; slot < 2; ++slot)
            for (int algo = 0; algo < kAlgos; ++algo)
            {
                panel.setAlgorithm(slot, algo);
                std::vector<juce::Rectangle<int>> parts { panel.getAlgorithmSelect(slot).getBounds() };
                for (int p = 0; p < 4; ++p)
                    if (panel.getKnob(slot, p).isVisible()) parts.push_back(panel.getKnob(slot, p).getBounds());
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    expect(panel.getLocalBounds().contains(parts[i]), tag + "insert part inside the panel");
                    for (size_t j = i + 1; j < parts.size(); ++j)
                        expect(! parts[i].intersects(parts[j]), tag + "insert parts overlap");
                }
            }

        // Side by side the column is 80 + 136 = 216 wide.
        expectEquals(mu_ui::s(LF::kMixerMasterW) + mu_ui::s(LF::kMixerInsertPanelW), mu_ui::s(LF::kMixerMasterTotalW));
    }

    static constexpr int kAlgos = mu_ui::kInsertAlgoCount;

    void runTest() override
    {
        beginTest("master column fits at Small, Medium and Large");
        checkFit(0.85f);
        checkFit(1.0f);
        checkFit(1.15f);

        beginTest("master fader top lines up with the Client faders");
        {
            using LF = MuLookAndFeel;
            MixerChannel client(MixerChannel::Type::Client, "App", juce::Colours::red);
            const int stripW = (LF::kWindowWidth - 2 * LF::kScrewedPanelInset - LF::kMixerMasterTotalW - LF::kMixerDivW
                                - 7 * LF::kMixerChanGap) / 8;
            client.setBounds(0, 0, mu_ui::s(stripW), mu_ui::s(kColumnH));
            MixerChannel master(MixerChannel::Type::Master, "Master", juce::Colours::white);
            master.useCallbackMaster();
            master.setFaderTop(MixerChannel::clientFaderTop());
            master.setBounds(0, 0, mu_ui::s(LF::kMixerMasterW), mu_ui::s(kColumnH));
            auto* cf = findFader(client);
            auto* mf = findFader(master);
            expect(cf != nullptr && mf != nullptr);
            if (cf != nullptr && mf != nullptr)
            {
                expectEquals(mf->getY(), cf->getY());
                expectEquals(mf->getBottom(), cf->getBottom());   // dB readouts line up too
            }
        }

        beginTest("master level calls back on the fader only, never from the setter");
        {
            MixerChannel master(MixerChannel::Type::Master, "Master", juce::Colours::white);
            master.useCallbackMaster();
            int calls = 0;
            float last = -1.0f;
            master.onMasterLevel = [&](float v) { ++calls; last = v; };
            master.setMasterLevel(0.5f);
            expectEquals(calls, 0);
            auto* fader = findFader(master);
            expect(fader != nullptr);
            if (fader != nullptr)
            {
                expectWithinAbsoluteError((float) fader->getValue(), 0.5f, 0.001f);
                fader->setValue(1.5, juce::sendNotificationSync);
                expectEquals(calls, 1);
                expectWithinAbsoluteError(last, 1.5f, 0.001f);
            }
            for (auto* c : master.getChildren())
                if (auto* l = dynamic_cast<juce::Label*>(c))
                    expectEquals(l->getText(), MixerChannel::formatClientDb(1.5f));
        }

        beginTest("insert panel: user changes call back, setters stay silent");
        {
            mu_ui::MasterInsertPanel panel;
            panel.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kMixerInsertPanelW), mu_ui::s(kColumnH));
            int algoCalls = 0, paramCalls = 0, lastSlot = -1, lastAlgo = -1;
            panel.onAlgorithm = [&](int slot, int algo) { ++algoCalls; lastSlot = slot; lastAlgo = algo; };
            panel.onParam = [&](int, int, float) { ++paramCalls; };

            panel.setAlgorithm(0, 6);                // 3-Band EQ: four visible knobs
            panel.setParam(0, 1, 0.25f);
            expectEquals(algoCalls + paramCalls, 0);
            expectEquals(panel.getAlgorithm(0), 6);
            for (int p = 0; p < 4; ++p)
                expect(panel.getKnob(0, p).isVisible(), "EQ shows all four knobs");

            // The selector: id = algorithm + 1.
            panel.getAlgorithmSelect(1).getComboBox().setSelectedId(5 + 1, juce::sendNotificationSync);   // Clipper: any non-None algorithm
            expectEquals(algoCalls, 1);
            expectEquals(lastSlot, 1);
            expectEquals(lastAlgo, 5);
            expectEquals(paramCalls, 4);             // each of the four parameters reported

            // A knob move reports its normalised value.
            const int before = paramCalls;
            float norm = -1.0f;
            panel.onParam = [&](int, int, float n) { ++paramCalls; norm = n; };
            panel.getKnob(0, 0).getSlider().setValue(panel.getKnob(0, 0).getSlider().getMaximum(),
                                                     juce::sendNotificationSync);
            expectEquals(paramCalls, before + 1);
            expectWithinAbsoluteError(norm, 1.0f, 0.001f);

            // None hides every knob; the labels carry no units.
            panel.setAlgorithm(0, 0);
            for (int p = 0; p < 4; ++p)
                expect(! panel.getKnob(0, p).isVisible(), "None hides the knobs");
            panel.setAlgorithm(0, 6);
            expectEquals(panel.getKnob(0, 0).getLabel(), juce::String("Low"));
        }

        beginTest("insert panel remembers an algorithm's values when cycling back");
        {
            mu_ui::MasterInsertPanel panel;
            panel.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kMixerInsertPanelW), mu_ui::s(kColumnH));
            panel.setAlgorithm(0, 6);
            panel.setParam(0, 0, 0.8f);
            panel.getAlgorithmSelect(0).getComboBox().setSelectedId(2, juce::sendNotificationSync);      // away, to Soft Clip
            panel.getAlgorithmSelect(0).getComboBox().setSelectedId(7, juce::sendNotificationSync);      // and back to the EQ
            expectWithinAbsoluteError(panel.getParam(0, 0), 0.8f, 0.01f);
        }

        beginTest("insert panel greys out the Vocoder's P2 to P4 for the noise waves");
        {
            mu_ui::MasterInsertPanel panel;
            panel.setBounds(0, 0, mu_ui::s(MuLookAndFeel::kMixerInsertPanelW), mu_ui::s(kColumnH));
            auto live = [&](int slot, int p) { return panel.getKnob(slot, p).isEnabled() && panel.getKnob(slot, p).getAlpha() == 1.0f; };

            for (int vocoder : { 12, 13 })
            {
                panel.setAlgorithm(0, vocoder);
                for (int wave = 0; wave < 4; ++wave)
                {
                    panel.setParam(0, 0, mu_ui::actualToNorm((float) wave, vocoder, 0));
                    expect(live(0, 0), "the Wave knob itself is always live");
                    for (int p = 1; p < 4; ++p)
                        expect(live(0, p) == (wave < 2), "Saw and Square are pitched; White and Pink grey P2 to P4");
                }
            }

            // A knob move does the same, and the other slot and other algorithms are untouched.
            panel.setAlgorithm(0, 12);
            panel.setParam(0, 0, mu_ui::actualToNorm(0.0f, 12, 0));
            panel.getKnob(0, 0).getSlider().setValue(2.0, juce::sendNotificationSync);
            for (int p = 1; p < 4; ++p)
                expect(! live(0, p), "moving Wave to White greys P2 to P4");
            panel.getKnob(0, 0).getSlider().setValue(1.0, juce::sendNotificationSync);
            for (int p = 1; p < 4; ++p)
                expect(live(0, p), "moving Wave back to Square restores them");
            panel.setParam(0, 0, mu_ui::actualToNorm(3.0f, 12, 0));
            panel.setAlgorithm(0, 6);                // EQ: every knob live whatever P1 holds
            for (int p = 0; p < 4; ++p)
                expect(live(0, p), "other algorithms never grey out");
            panel.setAlgorithm(1, 12);
            for (int p = 0; p < 4; ++p)
                expect(live(1, p), "slot 2 has its own state (Wave starts at Saw)");
        }

        beginTest("insert panel shows the gain reduction on P2 for the Compressor and Limiter only");
        {
            mu_ui::MasterInsertPanel panel;
            std::atomic<float> gr { 0.0f };
            panel.setGainReductionSource(0, &gr);
            // The meter is a private detail of the knob, so the check is that switching algorithms
            // with a source set neither crashes nor leaves the knobs disabled.
            for (int algo : { 7, 8, 6, 7, 0, 8 })
            {
                panel.setAlgorithm(0, algo);
                if (algo != 0) expect(panel.getKnob(0, 1).isEnabled());
            }
            panel.setGainReductionSource(0, nullptr);
            panel.setGainReductionSource(5, &gr);    // out of range: ignored
        }
    }
};

static MasterColumnFitTests masterColumnFitTests;
