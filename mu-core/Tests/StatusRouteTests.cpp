#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/LayerPanel.h"
#include "UI/LayerContent.h"
#include "UI/Components/KnobWithLabel.h"

// "The status bar shows the name and value of the last control touched, in every app, without fail." A layer's content reaches the status bar
// through the layer panel: a content that nobody gave a hook of its own is connected when it is shown. (mu-On's track content was never
// connected to the status bar, so its knobs said nothing; this pins the route.)
struct StatusRouteTests : juce::UnitTest
{
    StatusRouteTests() : juce::UnitTest("Status bar: a layer's content always reports to it", "mu-core") {}

    // A content with a knob that reports through the content's own hook, as every product's content does.
    struct KnobContent : LayerContent
    {
        KnobContent()
        {
            knob.setRange(0.0, 100.0, 1.0);
            knob.onStatusUpdate = [this](const juce::String& name, const juce::String& value) { if (onStatusUpdate) onStatusUpdate(name, value); };
            addAndMakeVisible(knob);
        }
        void resized() override { knob.setBounds(getLocalBounds()); }
        void setLayer(int) override {}
        void releaseModulationArcs() override {}
        KnobWithLabel knob { "Feedback" };
    };

    void runTest() override
    {
        beginTest("a content with no hook of its own reports through the panel's");
        {
            std::vector<std::pair<juce::String, juce::String>> shown;
            KnobContent content;
            expect(! content.onStatusUpdate, "nobody has wired it");
            mu_ui::routeContentStatus(&content, [&shown](const juce::String& n, const juce::String& v) { shown.emplace_back(n, v); });
            expect((bool) content.onStatusUpdate, "now it has a hook");
            content.knob.setValue(42.0, juce::sendNotificationSync);
            expectEquals((int) shown.size(), 1);
            if (! shown.empty())
            {
                expectEquals(shown[0].first, juce::String("Feedback"));
                expect(shown[0].second.contains("42"), "the value is shown: " + shown[0].second);
            }
        }

        beginTest("a content that already has its own hook keeps it");
        {
            int own = 0, routed = 0;
            KnobContent content;
            content.onStatusUpdate = [&own](const juce::String&, const juce::String&) { ++own; };
            mu_ui::routeContentStatus(&content, [&routed](const juce::String&, const juce::String&) { ++routed; });
            content.knob.setValue(7.0, juce::sendNotificationSync);
            expect(own >= 1 && routed == 0, "its own hook answered, the route did not");
        }
    }
};

static StatusRouteTests statusRouteTests;
