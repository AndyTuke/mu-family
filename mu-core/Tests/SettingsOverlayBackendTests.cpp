#include "TestShellBackend.h"
#include "UI/StandardSettingsOverlay.h"

#include <memory>

// The family settings page over a ShellBackend that is not a processor: the row switches, the
// master-volume row and the fit rule (design-ui-family section 5) with an app-style Options set.
namespace
{
using Overlay = mu_ui::StandardSettingsOverlay;

struct LevelBackend : TestShellBackend
{
    using TestShellBackend::TestShellBackend;
    float level = 0.25f;
    bool  hasMasterLevel() const override { return true; }
    float getMasterLevel() const override { return level; }
    void  setMasterLevel(float l) override { level = l; }
};

struct ScaleGuard
{
    float saved = mu_ui::scale;
    ~ScaleGuard() { mu_ui::scale = saved; }
};

template <class T> int countVisible(juce::Component& parent)
{
    int n = 0;
    for (auto* c : parent.getChildren())
        if (dynamic_cast<T*>(c) != nullptr && c->isVisible()) ++n;
    return n;
}

template <class T> T* firstOf(juce::Component& parent)
{
    for (auto* c : parent.getChildren())
        if (auto* t = dynamic_cast<T*>(c)) return t;
    return nullptr;
}
} // namespace

struct SettingsOverlayBackendTests : juce::UnitTest
{
    SettingsOverlayBackendTests() : juce::UnitTest("StandardSettingsOverlay over a ShellBackend", "mu-core") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        ScaleGuard guard;
        mu_ui::scale = 1.0f;

        beginTest("no master level: the Audio row is absent");
        {
            TestShellBackend none(true);
            Overlay page(none);
            expectEquals(countVisible<KnobWithLabel>(page), 0);
        }

        beginTest("master level: the knob shows the backend's level and writes it back");
        {
            LevelBackend lvl(true);
            Overlay page(lvl);
            auto* knob = firstOf<KnobWithLabel>(page);
            expect(knob != nullptr, "knob present");
            if (knob == nullptr) return;
            expectWithinAbsoluteError(knob->getSlider().getValue(), 0.25, 0.01);
            knob->getSlider().setValue(0.5, juce::sendNotificationSync);
            expectWithinAbsoluteError((double) lvl.level, 0.5, 0.01);
        }

        beginTest("defaults: standalone shows Tempo, Time signature and Source; Sync offset waits for MIDI In");
        {
            TestShellBackend fake(true);
            Overlay page(fake);
            expectEquals(countVisible<DropdownSelect>(page), 2);   // time signature + clock source
            expectEquals(countVisible<NudgeInput>(page), 1);       // tempo only
        }

        beginTest("a plugin backend has no meter and no clock rows");
        {
            TestShellBackend fake(false);
            Overlay page(fake);
            expectEquals(countVisible<DropdownSelect>(page), 0);
        }

        beginTest("app options: no Tempo, Meter or Source rows, Sync offset always");
        {
            TestShellBackend fake(true);
            Overlay::Options o;
            o.showTransport = false;
            o.showMeter = false;
            o.showClockSource = false;
            o.syncOffsetAlways = true;
            Overlay page(fake, o);
            expectEquals(countVisible<DropdownSelect>(page), 0);
            expectEquals(countVisible<NudgeInput>(page), 1);       // the Sync offset input
        }

        beginTest("Sync offset edits reach the backend");
        {
            TestShellBackend fake(true);
            Overlay::Options o;
            o.showTransport = false;
            o.syncOffsetAlways = true;
            Overlay page(fake, o);
            NudgeInput* offset = nullptr;
            for (auto* c : page.getChildren())
                if (auto* n = dynamic_cast<NudgeInput*>(c)) offset = n;
            expect(offset != nullptr, "sync offset input present");
            if (offset != nullptr) { offset->setValue(40); offset->onChange(40); }
            expectEquals(fake.syncOffset, 40);
        }

        beginTest("display size writes the backend's scale");
        {
            TestShellBackend fake(true);
            Overlay page(fake);
            auto* seg = firstOf<SegmentControl>(page);
            expect(seg != nullptr, "size control present");
            if (seg != nullptr) seg->onChange(1);
            expectWithinAbsoluteError(fake.uiScale, mu_core::ShellBackend::kUiScaleLarge, 0.0001f);
        }

        beginTest("an app-style page fits without scrolling at both sizes");
        for (float sc : { 1.0f, 1.25f })
        {
            mu_ui::scale = sc;
            LevelBackend fake(true);
            fake.syncEnabled = true;
            Overlay::Options o;
            o.showTransport = false;
            o.showMeter = false;
            o.showClockSource = false;
            o.syncOffsetAlways = true;
            Overlay page(fake, o);
            page.setBounds(0, 0, mu_ui::s(1088), mu_ui::s(774));
            for (auto* c : page.getChildren())
                if (c->isVisible() && ! c->getBounds().isEmpty())
                    expect(page.getLocalBounds().contains(c->getBounds()), "inside the overlay: " + c->getBounds().toString());
        }
    }
};

static SettingsOverlayBackendTests settingsOverlayBackendTests;
