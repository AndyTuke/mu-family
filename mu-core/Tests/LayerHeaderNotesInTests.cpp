// The header bar's Notes In control (docs/design-ui-family.md 13.10, mu-tronic/design-ui.md 4.8): opt-in and hidden until a
// product gives it accessors, hidden (not disabled) when told the layer's type does not read notes, items Omni then
// Channel 1 to 16 with the channel as id - 1, the selector re-reads its parameter on request, a pick writes through the
// product's setter, a pick under Performance mode is put back, and the bar's preset dropdown yields exactly the
// control's width. Driven without a display.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/LayerHeaderBar.h"

namespace
{
struct ScaleGuard
{
    float saved = mu_ui::scale;
    explicit ScaleGuard(float s) { mu_ui::scale = s; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// Performance mode on or off, the only part of the live view the control looks at (through the clip strip it shares a bind with).
struct ModeView : mu_core::LiveView
{
    bool perf = false;
    mu_pp::PerformanceFile performance;
    mutable juce::ListenerList<Listener> listeners;
    bool isPerformanceMode() const noexcept override { return perf; }
    const mu_pp::PerformanceFile& getPerformance() const noexcept override { return performance; }
    juce::File getPerformanceFile() const override { return {}; }
    int  getCurrentItem() const noexcept override { return -1; }
    int  getQueuedItem() const noexcept override { return -1; }
    int  getSelectedLayer() const noexcept override { return 0; }
    int  getLayerCount() const override { return 1; }
    int  getLayerColourIndex(int) const override { return 0; }
    bool isItemPlayable(int) const override { return false; }
    uint32_t getRevision() const noexcept override { return 0; }
    int  getPlayingClip(int) const override { return -1; }
    int  getQueuedClip(int) const override { return -1; }
    uint8_t getClipMask(int) const override { return 0; }
    bool isLayerMuted(int) const override { return false; }
    void addListener(Listener* l) const override { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }
};

struct NullSink : mu_core::ControlSink
{
    bool perform(const mu_core::ControlAction&) override { return false; }
};
}

class LayerHeaderNotesInTests : public juce::UnitTest
{
public:
    LayerHeaderNotesInTests() : juce::UnitTest("Header bar: the Notes In channel control", "mu-core") {}

    void runTest() override
    {
        beginTest("hidden by default and until a product gives it accessors; hidden (not disabled) when the layer's type ignores notes");
        {
            LayerHeaderBar bar;
            bar.setBounds(0, 0, 1000, 28);
            expect(! bar.isNotesInShown(), "off by default");
            bar.setShowNotesIn(true);
            expect(! bar.isNotesInShown(), "wanted, but with no accessors there is nothing to show");

            int channel = 0;
            bar.setNotesInControl([&] { return channel; }, [&](int c) { channel = c; });
            expect(bar.isNotesInShown(), "opted in and wanted");
            bar.setShowNotesIn(false);
            expect(! bar.isNotesInShown() && ! bar.getNotesInDropdown().isVisible(), "a type that ignores notes: hidden");
            expect(bar.getNotesInDropdown().isEnabled(), "hidden, not disabled");
            bar.setShowNotesIn(true);
            expect(bar.isNotesInShown());

            bar.setNotesInControl(nullptr, nullptr);
            expect(! bar.isNotesInShown(), "taking the accessors away hides it again");
        }

        beginTest("items: Omni, then Channel 1 to 16, the channel being id - 1");
        {
            LayerHeaderBar bar;
            auto& box = bar.getNotesInDropdown().getComboBox();
            expectEquals(box.getNumItems(), 17);
            expectEquals(box.getItemText(0), juce::String("Omni"));
            expectEquals(box.getItemId(0), 1);
            expectEquals(box.getItemText(1), juce::String("Channel 1"));
            expectEquals(box.getItemId(1), 2);
            expectEquals(box.getItemText(16), juce::String("Channel 16"));
            expectEquals(box.getItemId(16), 17);
        }

        beginTest("the selector re-reads the layer's channel on request: another slot of the same type shows its own");
        {
            LayerHeaderBar bar;
            int channel = 0;
            bar.setNotesInControl([&] { return channel; }, [&](int c) { channel = c; });
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 1, "Omni");
            channel = 5;
            bar.refreshNotesIn();
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 6, "Channel 5");
            channel = 16;
            bar.refreshNotesIn();
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 17);
            channel = 99;   // a stray value is clamped, never an item that does not exist
            bar.refreshNotesIn();
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 17);
            channel = -3;
            bar.refreshNotesIn();
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 1);
        }

        beginTest("a pick writes through the setter and says what it did on the status line");
        {
            LayerHeaderBar bar;
            int channel = 0;
            std::vector<int> written;
            juce::StringArray lines;
            bar.setNotesInControl([&] { return channel; }, [&](int c) { channel = c; written.push_back(c); });
            bar.onNotesInStatus = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };

            bar.getNotesInDropdown().getComboBox().setSelectedId(4, juce::sendNotificationSync);   // Channel 3
            expect(written == std::vector<int> { 3 });
            expectEquals(channel, 3);
            expect(lines.contains("Notes In|Channel 3 only"), lines.joinIntoString(" / "));

            bar.getNotesInDropdown().getComboBox().setSelectedId(1, juce::sendNotificationSync);   // Omni
            expectEquals(channel, 0);
            expect(lines.contains("Notes In|Omni (all channels)"), lines.joinIntoString(" / "));
        }

        beginTest("under Performance mode a pick is put back and writes nothing (routing is set before the gig)");
        {
            ModeView view;
            NullSink sink;
            LayerHeaderBar bar;
            bar.bindClips(&view, &sink);   // the header reads the mode through the clip strip's bind
            int channel = 2;
            int writes = 0;
            bar.setNotesInControl([&] { return channel; }, [&](int c) { channel = c; ++writes; });
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 3);

            view.perf = true;
            bar.getNotesInDropdown().getComboBox().setSelectedId(10, juce::sendNotificationSync);
            expectEquals(writes, 0);
            expectEquals(channel, 2);
            expectEquals(bar.getNotesInDropdown().getSelectedId(), 3, "put back to what the layer has");

            view.perf = false;
            bar.getNotesInDropdown().getComboBox().setSelectedId(10, juce::sendNotificationSync);
            expectEquals(channel, 9, "and normal again once the mode is off");
        }

        beginTest("the status wording");
        expectEquals(LayerHeaderBar::notesInText(0), juce::String("Omni (all channels)"));
        expectEquals(LayerHeaderBar::notesInText(1), juce::String("Channel 1 only"));
        expectEquals(LayerHeaderBar::notesInText(16), juce::String("Channel 16 only"));
        expectEquals(LayerHeaderBar::notesInText(17), juce::String("Omni (all channels)"), "out of range reads as Omni");

        beginTest("layout: the preset dropdown yields exactly the caption, the selector and their gaps, at both sizes, with and without the clip strip");
        for (const float scale : { 1.0f, 1.25f })
        for (const bool clips : { false, true })
        {
            ScaleGuard guard(scale);
            using mu_ui::s;
            using LF = MuLookAndFeel;
            LayerHeaderBar bar;
            bar.setBounds(0, 0, s(1080), s(LayerHeaderBar::kHeight));
            bar.setShowClips(clips);
            const int plainW = bar.getPresetDropdown().getWidth();

            int channel = 0;
            bar.setNotesInControl([&] { return channel; }, [&](int c) { channel = c; });
            auto& dd  = bar.getPresetDropdown();
            auto& sel = bar.getNotesInDropdown();
            expectEquals(sel.getWidth(), s(LF::kNotesInW), "the selector's ruled width");
            expectEquals(sel.getHeight(), s(LF::kStripDisplayH), "and height");
            expectEquals(plainW - dd.getWidth(), s(LF::kNotesInLabelW) + s(4) + s(LF::kNotesInW) + s(LF::kClipStripGap),
                         "the dropdown gives up caption + 4 + selector + the strip gap (148 at Medium)");
            expect(sel.getX() >= dd.getRight() + s(LF::kClipStripGap) + s(LF::kNotesInLabelW) + s(4), "caption and selector right of the dropdown");
            if (clips)
            {
                auto& strip = bar.getClipStrip();
                expectEquals(strip.getX() - sel.getRight(), s(LF::kClipStripGap), "the clip strip's gap after the selector");
                expect(sel.getRight() <= strip.getX(), "the selector is left of the clip strip");
            }
            expect(dd.getWidth() >= s(80), "the dropdown's floor is untouched");

            bar.setShowNotesIn(false);
            expectEquals(bar.getPresetDropdown().getWidth(), plainW, "hidden: the dropdown has its width back");
        }
        {
            ScaleGuard guard(1.0f);
            LayerHeaderBar bar;
            bar.setBounds(0, 0, 1080, LayerHeaderBar::kHeight);
            bar.setShowClips(true);
            const int plainW = bar.getPresetDropdown().getWidth();
            bar.setNotesInControl([] { return 0; }, [](int) {});
            expectEquals(plainW - bar.getPresetDropdown().getWidth(), 148, "148 at Medium, as ruled");
        }

        beginTest("the longest item against the 92 px selector: measured, and reported (the ruling asks for a picture, not a local fix)");
        {
            ScaleGuard guard(1.0f);
            const juce::Font font = MuLookAndFeel::lcdFont(mu_ui::sf((float) MuLookAndFeel::kLcdTextH));
            juce::GlyphArrangement ga;
            ga.addLineOfText(font, "Channel 16", 0.0f, 0.0f);
            const int textW = (int) std::ceil(ga.getBoundingBox(0, ga.getNumGlyphs(), true).getWidth());
            // The combo box's label is inset 6 px at the left and leaves 24 px at the right for the arrow; a label keeps a
            // 5 px border either side of its text.
            const int room = MuLookAndFeel::kNotesInW - 24 - 2 * 5;
            logMessage("Channel 16 needs " + juce::String(textW) + " px of the " + juce::String(room) + " the 92 px selector leaves"
                       + (textW > room ? ": it does NOT fit (design-ui 4.8 asks for a report; fallback 'Ch 16' in the closed display only)" : ""));
            expect(textW > 0 && room > 0);
        }
    }
};

static LayerHeaderNotesInTests sLayerHeaderNotesInTests;
