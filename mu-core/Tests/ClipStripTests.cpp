// The layer's clip strip (docs/design-ui-family.md section 13): the lamp state per row of the 13.2 table, the
// gesture table of 13.3 against a fake sink, the strip's width and place in the header bar at both UI sizes, the
// strip hidden unless a product asks for it, read-only under Performance mode and its shield, and the Reset
// confirm's wording. All driven with a fake LiveView and ControlSink, so no processor or display is needed.

#include <juce_gui_basics/juce_gui_basics.h>
#include "UI/ClipStrip.h"
#include "UI/LayerHeaderBar.h"
#include "UI/ShellChrome.h"

#include <cmath>
#include <set>

namespace
{
using namespace mu_core;
using Lamp = ClipStrip::Lamp;

struct ScaleGuard
{
    float saved = mu_ui::scale;
    explicit ScaleGuard(float s) { mu_ui::scale = s; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// A LiveView with settable clip state for four layers.
struct FakeView : LiveView
{
    static constexpr int kLayers = 4;
    bool perf = false;
    int playing[kLayers] { -1, -1, -1, -1 }, queued[kLayers] { -1, -1, -1, -1 };
    uint8_t mask[kLayers] {};
    uint8_t foreign[kLayers] {};   // bit n set = the layer's clip n is of another layer type than the layer's
    bool muted[kLayers] {};
    bool clipboardEmpty = true, pasteFits = true, fullStaged = false;   // the clip clipboard and a staged full preset
    mu_pp::PerformanceFile performance;
    mutable juce::ListenerList<Listener> listeners;

    bool isPerformanceMode() const noexcept override { return perf; }
    const mu_pp::PerformanceFile& getPerformance() const noexcept override { return performance; }
    juce::File getPerformanceFile() const override { return {}; }
    int  getCurrentItem() const noexcept override { return -1; }
    int  getQueuedItem() const noexcept override { return -1; }
    int  getSelectedLayer() const noexcept override { return 0; }
    int  getLayerCount() const override { return kLayers; }
    int  getLayerColourIndex(int layer) const override { return layer; }
    bool isItemPlayable(int) const override { return false; }
    uint32_t getRevision() const noexcept override { return 0; }
    int  getPlayingClip(int l) const override { return l >= 0 && l < kLayers ? playing[l] : -1; }
    int  getQueuedClip(int l) const override { return l >= 0 && l < kLayers ? queued[l] : -1; }
    uint8_t getClipMask(int l) const override { return l >= 0 && l < kLayers ? mask[l] : (uint8_t) 0; }
    bool isLayerMuted(int l) const override { return l >= 0 && l < kLayers && muted[l]; }
    bool isClipForLayer(int l, int c) const override { return l < 0 || l >= kLayers || ((foreign[l] >> c) & 1) == 0; }
    bool isClipboardEmpty() const override { return clipboardEmpty; }
    bool canPasteClip(int) const override { return ! clipboardEmpty && pasteFits; }
    bool isFullPresetStaged() const override { return fullStaged; }
    juce::String names[kLayers][8];
    juce::String getClipName(int l, int c) const override
    { return l >= 0 && l < kLayers && c >= 0 && c < 8 ? names[l][c] : juce::String(); }
    void addListener(Listener* l) const override { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }
    void changed() { listeners.call([](Listener& l) { l.liveStateChanged(); }); }
};

// Records what the strip asks for; an optional hook plays the owner (e.g. a mute flips the view).
struct FakeSink : ControlSink
{
    std::vector<ControlAction> got;
    std::function<void(const ControlAction&)> hook;
    bool perform(const ControlAction& a) override
    {
        got.push_back(a);
        if (hook) hook(a);
        return true;
    }
};

struct Asked { juce::String title, message, button; std::function<void()> go; };

// A strip on layer 1 ("Kick") over a fake view and sink, with the dialog and the status bar captured.
struct Rig
{
    FakeView view;
    FakeSink sink;
    ClipStrip strip;
    std::vector<Asked> asked;
    juce::StringArray lines;

    Rig()
    {
        strip.bind(&view, &sink);
        strip.setLayer(1);
        strip.setLayerName("Kick");
        strip.setNoun("Voice");
        strip.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
        strip.askConfirm = [this](const juce::String& t, const juce::String& m, const juce::String& b, std::function<void()> go)
        {
            asked.push_back({ t, m, b, std::move(go) });
        };
    }

    // The stored clips, the playing one and the queued one on layer 1.
    void set(std::initializer_list<int> stored, int play = -1, int queue = -1, bool mute = false)
    {
        uint8_t m = 0;
        for (int c : stored) m = (uint8_t) (m | (1 << c));
        view.mask[1] = m; view.playing[1] = play; view.queued[1] = queue; view.muted[1] = mute;
        view.changed();
    }
    bool sent(ControlActionType t, int pad) const
    {
        return ! sink.got.empty() && sink.got.back().type == t && sink.got.back().layer == 1 && sink.got.back().index == pad;
    }
};

struct Host : juce::Component {};
}

class ClipStripTests : public juce::UnitTest
{
public:
    ClipStripTests() : juce::UnitTest("Clip strip", "mu-core") {}

    void runTest() override
    {
        beginTest("lamp state per row of the table: playing, available, empty, queued, muted playing");
        {
            const uint8_t stored = 0b0010'1110;   // clips 1, 2, 3 and 5
            expect(ClipStrip::lampFor(2, 2, -1, stored, false) == Lamp::Current, "playing");
            expect(ClipStrip::lampFor(2, 2, -1, stored, true) == Lamp::MutedCurrent, "playing on a muted layer");
            expect(ClipStrip::lampFor(1, 2, -1, stored, false) == Lamp::Available, "stored, not playing");
            expect(ClipStrip::lampFor(0, 2, -1, stored, false) == Lamp::Empty, "nothing stored");
            expect(ClipStrip::lampFor(5, 2, 5, stored, false) == Lamp::Queued, "queued");
            expect(ClipStrip::lampFor(5, 2, 5, stored, true) == Lamp::Queued, "a muted layer still shows its queued pad pulsing");
            expect(ClipStrip::lampFor(2, 2, 5, stored, true) == Lamp::MutedCurrent, "and its playing pad flashing");
            expect(ClipStrip::lampFor(1, -1, -1, stored, false) == Lamp::Available, "none playing: stored pads are dim, none bright");
            expect(ClipStrip::lampFor(3, -1, -1, stored, true) == Lamp::Available, "a muted layer with none playing flashes nothing");
        }

        beginTest("lamp levels: playing lampOn, available lampDim, empty lampOff, queued pulses at 2 Hz, muted flashes at 1 Hz");
        {
            const auto& L = MuLookAndFeel::lighting();
            const float bright = juce::jmin(1.0f, L.lampOn + L.lampPlayhead);
            expectEquals(mu_ui::lampLit(Lamp::Current, 0.0, false), L.lampOn);
            expectEquals(mu_ui::lampLit(Lamp::Current, 0.0, true), bright);   // just started
            expectEquals(mu_ui::lampLit(Lamp::Available, 0.0, false), L.lampDim);
            expectEquals(mu_ui::lampLit(Lamp::Empty, 0.0, false), L.lampOff);
            expectWithinAbsoluteError(mu_ui::lampLit(Lamp::Queued, 0.0, false), L.lampDim, 0.001f);
            expectWithinAbsoluteError(mu_ui::lampLit(Lamp::Queued, 1000.0 / (2.0 * L.queuedPulseHz), false), bright, 0.001f);

            // Muted: a hard square wave at clipMutedFlashHz, 50 % duty, and its dim half is never "empty".
            const double period = 1000.0 / (double) L.clipMutedFlashHz;
            expectEquals(mu_ui::lampLit(Lamp::MutedCurrent, 0.0, false), L.lampOn);
            expectEquals(mu_ui::lampLit(Lamp::MutedCurrent, period * 0.49, false), L.lampOn);
            expectEquals(mu_ui::lampLit(Lamp::MutedCurrent, period * 0.51, false), L.lampDim);
            expectEquals(mu_ui::lampLit(Lamp::MutedCurrent, period * 1.25, false), L.lampOn);
            expect(mu_ui::lampLit(Lamp::MutedCurrent, period * 0.75, false) > L.lampOff, "the dim half is not off");

            // Never alike: over a second the flash takes exactly two values, the pulse sweeps through many.
            std::set<int> flashLevels, pulseLevels;
            for (int ms = 0; ms < 1000; ms += 25)
            {
                flashLevels.insert((int) std::lround(mu_ui::lampLit(Lamp::MutedCurrent, (double) ms, false) * 1000.0f));
                pulseLevels.insert((int) std::lround(mu_ui::lampLit(Lamp::Queued, (double) ms, false) * 1000.0f));
            }
            expectEquals((int) flashLevels.size(), 2, "hard on / off");
            expect(pulseLevels.size() > 6, "smooth sweep");
        }

        beginTest("the strip follows the live view and a new playing clip flashes once");
        {
            Rig r;
            r.set({ 0, 2, 4 }, 2);
            expect(r.strip.getLamp(0) == Lamp::Available && r.strip.getLamp(2) == Lamp::Current && r.strip.getLamp(1) == Lamp::Empty);
            r.set({ 0, 2, 4 }, 2, 4);
            expect(r.strip.getLamp(4) == Lamp::Queued && r.strip.getLamp(2) == Lamp::Current);
            r.set({ 0, 2, 4 }, 2, 4, true);
            expect(r.strip.getLamp(2) == Lamp::MutedCurrent && r.strip.getLamp(4) == Lamp::Queued);
            r.set({ 0, 2, 4 }, 4, -1, false);   // the queued clip landed
            expect(r.strip.getLamp(4) == Lamp::Current && r.strip.getLamp(2) == Lamp::Available);
            r.set({ 0, 2 }, -1);                // cleared while playing: nothing plays
            expect(r.strip.getLamp(4) == Lamp::Empty && r.strip.getLamp(2) == Lamp::Available);

            r.view.playing[0] = 3;   // another layer: not this strip's business
            r.view.mask[0] = 0xff;
            r.view.changed();
            expect(r.strip.getLamp(3) == Lamp::Empty);

            r.strip.setLayer(0);
            expect(r.strip.getLamp(3) == Lamp::Current, "rebinding shows the other layer");
            r.strip.setLayer(1);
            expect(r.strip.getLamp(3) == Lamp::Empty && r.strip.getLamp(2) == Lamp::Available);
        }

        beginTest("click: a stored pad launches, the playing pad is the sink's mute, a queue behind it is the sink's cancel");
        {
            Rig r;
            r.set({ 0, 2, 4 }, 2);
            r.strip.clickPad(4);
            expect(r.sent(ControlActionType::LaunchClip, 4), "launch");
            expectEquals((int) r.sink.got.size(), 1);
            expect(r.asked.empty(), "no confirm");

            // The playing pad: one LaunchClip, which the owner turns into a mute; the strip says what happened.
            r.sink.hook = [&](const ControlAction&) { r.view.muted[1] = ! r.view.muted[1]; r.view.changed(); };
            r.sink.got.clear();
            r.strip.clickPad(2);
            expect(r.sent(ControlActionType::LaunchClip, 2), "mute is a launch of the playing clip");
            expect(r.lines.contains("Mute|Kick muted"), "muted");
            r.strip.clickPad(2);
            expect(r.lines.contains("Mute|Kick unmuted"), "unmuted");

            // With another clip queued, the same press cancels the queue; it is not announced as a mute.
            r.sink.hook = nullptr;
            r.set({ 0, 2, 4 }, 2, 4);
            r.lines.clear();
            r.strip.clickPad(2);
            expect(r.sent(ControlActionType::LaunchClip, 2));
            expect(! r.lines.contains("Mute|Kick muted") && ! r.lines.contains("Mute|Kick unmuted"), "no mute line for a cancel");

            // The queued pad: nothing new for the strip to decide; the sink treats it as already on its way.
            r.sink.got.clear();
            r.strip.clickPad(4);
            expect(r.sent(ControlActionType::LaunchClip, 4));
        }

        beginTest("click on an empty pad stores without asking; a stored pad is never overwritten by a store");
        {
            Rig r;
            r.set({ 0, 2 }, 2);

            r.strip.clickPad(5);
            expect(r.sent(ControlActionType::StoreClip, 5), "empty pad: store");
            expect(r.asked.empty(), "and no confirm: nothing is lost");
            expect(r.lines.contains("Clip|Stored Kick clip 6"), "status line");

            r.sink.got.clear();
            r.strip.storePad(0);   // stored, not playing
            r.strip.storePad(2);   // the playing clip
            expect(r.sink.got.empty() && r.asked.empty(), "store only ever writes an empty pad");
        }

        beginTest("clear asks first and only works on a stored clip; the menu results map to their actions");
        {
            Rig r;
            r.set({ 0, 2 }, 2);

            r.strip.clearPad(5);
            expect(r.sink.got.empty() && r.asked.empty(), "an empty pad has nothing to clear");

            r.strip.clearPad(2);
            expectEquals((int) r.asked.size(), 1);
            expectEquals(r.asked[0].title, juce::String("Clear Clip"));
            expectEquals(r.asked[0].message, juce::String("Clear clip 3 of \"Kick\"?\nThis cannot be undone."));
            expectEquals(r.asked[0].button, juce::String("Clear"));
            expect(r.sink.got.empty(), "not before the answer");
            r.asked[0].go();
            expect(r.sent(ControlActionType::ClearClip, 2), "cleared the playing clip");
            expect(r.lines.contains("Clip|Cleared Kick clip 3"));

            r.sink.got.clear();
            r.asked.clear();
            r.strip.menuChosen(5, ClipStrip::menuStore);
            expect(r.sent(ControlActionType::StoreClip, 5) && r.asked.empty(), "menu Store here on an empty pad");
            r.sink.got.clear();
            r.strip.menuChosen(0, ClipStrip::menuStore);
            expect(r.sink.got.empty() && r.asked.empty(), "Store here does nothing on a stored pad");
            r.strip.menuChosen(0, ClipStrip::menuClear);
            expect(r.asked.size() == 1 && r.asked[0].title == "Clear Clip", "menu Clear asks");
            r.asked.clear();
            r.strip.menuChosen(0, 0);
            expect(r.sink.got.empty() && r.asked.empty(), "dismissed: nothing");
        }

        beginTest("a bad pad, no sink or no view does nothing; a confirm answered after the layer changed writes nothing");
        {
            Rig r;
            r.set({ 0 }, 0);
            r.strip.clickPad(-1); r.strip.clickPad(8); r.strip.storePad(9); r.strip.clearPad(-3);
            expect(r.sink.got.empty() && r.asked.empty());

            r.set({ 0, 1 }, 0);
            r.strip.clearPad(1);   // stored, not playing: asks
            expectEquals((int) r.asked.size(), 1);
            r.strip.setLayer(2);
            r.asked[0].go();
            expect(r.sink.got.empty(), "the layer changed while the dialog was open");

            ClipStrip loose;
            loose.clickPad(0); loose.storePad(0); loose.clearPad(0); loose.pastePad(0); loose.copyPad(0); loose.cutPad(0);   // nothing bound: no crash
            expect(loose.getLamp(0) == Lamp::Empty);
        }

        beginTest("Performance mode: the strip is read-only but keeps showing the live state");
        {
            Rig r;
            r.set({ 0, 2 }, 2);
            r.view.perf = true;
            r.view.changed();
            expect(r.strip.isReadOnly());
            r.view.clipboardEmpty = false;
            r.strip.clickPad(0); r.strip.clickPad(5);
            r.strip.storePad(5); r.strip.copyPad(0); r.strip.cutPad(0); r.strip.pastePad(5); r.strip.clearPad(2);
            r.strip.menuChosen(2, ClipStrip::menuClear);
            expect(r.sink.got.empty() && r.asked.empty(), "no press does anything");

            r.set({ 0, 2 }, 0, 2);   // the surface launched clip 0 and queued 2
            expect(r.strip.getLamp(0) == Lamp::Current && r.strip.getLamp(2) == Lamp::Queued, "it still follows the surface");
            expect(r.lines.contains("Queued|Kick clip 3 (at the next swap point)"), "and shows the queued line");
            r.set({ 0, 2 }, 2);
            expect(r.lines[r.lines.size() - 1] == "|", "the line goes when the clip lands");

            r.view.perf = false;
            r.view.changed();
            r.strip.clickPad(5);
            expect(r.sent(ControlActionType::StoreClip, 5), "leaving the mode gives the strip back");
        }

        beginTest("under the shield the pointer never reaches the strip, which is not an exempt rectangle");
        {
            ScaleGuard guard(1.0f);
            Host host;
            LayerHeaderBar bar;
            host.addAndMakeVisible(bar);
            host.setSize(1100, 40);
            bar.setBounds(0, 6, 1000, 28);
            bar.setShowClips(true);
            mu_ui::ShellChrome::InputShield shield;
            host.addChildComponent(shield);
            shield.setBounds(host.getLocalBounds());
            host.setVisible(true);

            auto& strip = bar.getClipStrip();
            const auto pad = ClipStrip::computeLayout(strip.getLocalBounds()).pads[3].getCentre();
            const auto at = host.getLocalPoint(&strip, pad);
            expect(host.getComponentAt(at) == &strip, "reachable with the shield down");
            shield.setVisible(true);
            expect(host.getComponentAt(at) == &shield, "swallowed with the shield up");
        }

        beginTest("strip width: 30 + 8 x 24 + 7 x 3 = 243, 251 with the gap to the dropdown; the Large size scales every part");
        {
            {
                ScaleGuard guard(1.0f);
                expectEquals(ClipStrip::stripWidth(), 243);
                expectEquals(ClipStrip::widthWithGap(), 251);
                const auto l = ClipStrip::computeLayout({ 0, 0, 243, 28 });
                expectEquals(l.caption.getWidth(), 30);
                expectEquals(l.pads[0].getX(), 30);
                expectEquals(l.pads[1].getX() - l.pads[0].getRight(), 3, "pads keep their gap");
                expectEquals(l.pads[7].getRight(), 243, "the last pad ends at the strip's edge");
                expectEquals(l.pads[0].getHeight(), MuLookAndFeel::kStripDisplayH);
                expectEquals(l.pads[0].getY(), (28 - 20) / 2, "centred in the bar");
            }
            {
                ScaleGuard guard(1.25f);
                using mu_ui::s;
                expectEquals(ClipStrip::stripWidth(), s(30) + 8 * s(24) + 7 * s(3));
                const auto l = ClipStrip::computeLayout({ 0, 0, ClipStrip::stripWidth(), s(28) });
                expectEquals(l.pads[7].getRight(), ClipStrip::stripWidth());
                expectEquals(l.pads[0].getHeight(), s(MuLookAndFeel::kStripDisplayH));
            }
        }

        beginTest("header bar: no strip unless asked for; with it the strip sits between the dropdown and Save at both sizes");
        for (const float scale : { 1.0f, 1.25f })
        {
            ScaleGuard guard(scale);
            using mu_ui::s;
            LayerHeaderBar bar;
            const int w = s(1080);   // about the header's width at the Medium window
            bar.setBounds(0, 0, w, s(LayerHeaderBar::kHeight));

            expect(! bar.getShowClips() && ! bar.getClipStrip().isVisible(), "hidden by default");
            const int plainRight = bar.getPresetDropdown().getRight();
            const int plainW     = bar.getPresetDropdown().getWidth();

            bar.setShowClips(true);
            auto& strip = bar.getClipStrip();
            auto& dd    = bar.getPresetDropdown();
            expect(strip.isVisible() && strip.getWidth() == ClipStrip::stripWidth(), "shown, at its fixed width");
            expectEquals(strip.getHeight(), s(MuLookAndFeel::kStripDisplayH), "no extra height");
            expect(strip.getX() >= dd.getRight() + s(MuLookAndFeel::kClipStripGap), "the gap to the dropdown");
            expect(strip.getRight() <= w && strip.getY() >= 0 && strip.getBottom() <= bar.getHeight(), "inside the bar");
            expectEquals(plainW - dd.getWidth(), ClipStrip::widthWithGap(), "the dropdown yields exactly the strip and its gap");
            expectEquals(plainRight - dd.getRight(), ClipStrip::widthWithGap());
            expect(dd.getWidth() >= s(500), "the dropdown keeps most of its width");
            for (auto* c : bar.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*>(c))
                    if (b->isVisible() && b->getButtonText() == "Save")
                        expect(strip.getRight() <= b->getX(), "left of Save");

            bar.setShowClips(false);
            expect(! bar.getClipStrip().isVisible() && bar.getPresetDropdown().getWidth() == plainW, "off again: the dropdown is back");

            // A bar too narrow for the strip keeps the dropdown's floor rather than collapsing it.
            bar.setBounds(0, 0, s(420), s(LayerHeaderBar::kHeight));
            bar.setShowClips(true);
            expect(bar.getPresetDropdown().getWidth() >= s(80), "the dropdown's floor is untouched");
        }

        beginTest("a stored clip of another layer type is refused: the status line says so, nothing is staged, no light changes");
        {
            Rig r;
            r.set({ 0, 2, 4 }, 2);
            r.view.foreign[1] = 0b0001'0000;   // layer 2's clip 5 is another type's (a Kick clip in a slot that became a Snare)
            r.view.changed();
            r.lines.clear();
            const auto before = r.strip.getLamp(4);
            r.strip.clickPad(4);
            expect(r.sink.got.empty(), "nothing is staged");
            expect(r.lines.contains("Clip|Clip 5 is for a different layer type"), r.lines.joinIntoString(" / "));
            expect(r.strip.getLamp(4) == before, "no light changes");

            r.lines.clear();
            r.strip.clickPad(0);
            expect(r.sent(ControlActionType::LaunchClip, 0), "a clip of the layer's own type still launches");
            expect(! r.lines.contains("Clip|Clip 1 is for a different layer type"));

        }

        runMenuTests();

        beginTest("Reset confirm: the clips line appears only when the bank is not empty");
        {
            expectEquals(LayerHeaderBar::resetConfirmMessage("Kick"),
                         juce::String("Reset \"Kick\" to defaults?\nThis also resets its mixer strip.\nThis cannot be undone."));
            expectEquals(LayerHeaderBar::resetConfirmMessage("Kick", 0), LayerHeaderBar::resetConfirmMessage("Kick"));
            expectEquals(LayerHeaderBar::resetConfirmMessage("Kick", 3),
                         juce::String("Reset \"Kick\" to defaults?\nThis also resets its mixer strip.\nThis also clears its clips.\nThis cannot be undone."));
        }

        beginTest("display name: the queued clip's name (queued), else the playing clip's, else nothing");
        {
            FakeView v;
            v.names[1][2] = "Intro"; v.names[1][5] = "Drop";
            using DN = ClipStrip::DisplayName;
            expect(ClipStrip::displayNameFor(v, 1) == DN {}, "nothing playing or queued");
            v.playing[1] = 2;
            expect(ClipStrip::displayNameFor(v, 1) == DN { "Intro", false }, "the playing clip, normal colour");
            v.queued[1] = 5;
            expect(ClipStrip::displayNameFor(v, 1) == DN { "Drop", true }, "the queued clip wins, flagged queued");
            v.queued[1] = 6;
            expect(ClipStrip::displayNameFor(v, 1) == DN { "clip 7", true }, "a queued clip with no name is still shown");
            v.queued[1] = -1; v.playing[1] = 6;
            expect(ClipStrip::displayNameFor(v, 1) == DN {}, "an unnamed playing clip leaves the box alone");
            expect(ClipStrip::displayNameFor(v, 9) == DN {}, "a layer out of range");
        }

        beginTest("display name: the strip tells the box once per change, and the header's box shows it (queued in the queued colour)");
        {
            Rig r;
            r.view.names[1][1] = "Intro"; r.view.names[1][3] = "Drop";
            std::vector<std::pair<juce::String, bool>> told;
            r.strip.onDisplayName = [&told](const juce::String& t, bool q) { told.push_back({ t, q }); };
            r.set({ 1, 3 }, -1, -1);
            told.clear();
            r.strip.setLayer(0); r.strip.setLayer(1);   // a new picture is told once even when nothing changed
            expectEquals((int) told.size(), 2);

            told.clear();
            r.set({ 1, 3 }, 1, -1);
            r.set({ 1, 3 }, 1, -1);   // no change: silent
            r.set({ 1, 3 }, 1, 3);
            r.set({ 1, 3 }, 3, -1);   // the queued clip landed
            expectEquals((int) told.size(), 3);
            expectEquals(told[0].first, juce::String("Intro")); expect(! told[0].second);
            expectEquals(told[1].first, juce::String("Drop"));  expect(told[1].second);
            expectEquals(told[2].first, juce::String("Drop"));  expect(! told[2].second);

            // The header bar wires the strip to its preset box.
            LayerHeaderBar bar;
            bar.setShowClips(true);
            bar.bindClips(&r.view, &r.sink);
            bar.setClipLayer(1);
            bar.setPresetFiles({ juce::File("a.muRhythm"), juce::File("b.muRhythm") });
            auto& dd = bar.getPresetDropdown();
            r.set({ 1, 3 }, 3, -1);
            expectEquals(dd.getText(), juce::String("Drop"));
            expect(dd.hasDisplayOverride() && ! dd.isDisplayQueued(), "playing: normal colour");
            r.set({ 1, 3 }, 3, 1);
            expectEquals(dd.getText(), juce::String("Intro"));
            expect(dd.isDisplayQueued(), "queued: the queued colour");
            dd.setSelectedId(0);   // the panel clears the selection on a rebind
            expectEquals(dd.getText(), juce::String("Intro"), "survives the panel's rebind");
            dd.setSelectedId(2);   // a file picked in code takes over
            expectEquals(dd.getText(), juce::String("b"));
            expect(! dd.hasDisplayOverride());
            r.set({ 1, 3 }, -1, -1);
            expect(! dd.hasDisplayOverride(), "nothing to name: the box is its own again");
            expectEquals(dd.getText(), juce::String("b"));
        }

        beginTest("painting: a playing pad is lit, an empty one is not, at both sizes");
        for (const float scale : { 1.0f, 1.25f })
        {
            ScaleGuard guard(scale);
            Rig r;
            r.strip.setColour(juce::Colour(0xff40c070));
            r.set({ 0, 1 }, 0);
            r.strip.setBounds(0, 0, ClipStrip::stripWidth(), mu_ui::s(28));
            r.strip.setVisible(true);

            const auto img = r.strip.createComponentSnapshot(r.strip.getLocalBounds(), false, 1.0f);

            const auto l = ClipStrip::computeLayout(r.strip.getLocalBounds());
            auto brightness = [&](int pad)
            {
                // The pad's left edge at mid height, clear of the number engraved in the middle.
                const auto c = img.getPixelAt(l.pads[(size_t) pad].getX() + 3, l.pads[(size_t) pad].getCentreY());
                return c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue();
            };
            const juce::String seen = " (playing " + juce::String(brightness(0)) + ", stored " + juce::String(brightness(1))
                                    + ", empty " + juce::String(brightness(5)) + ")";
            expect(brightness(0) > brightness(1), "playing brighter than stored" + seen);
            expect(brightness(1) > brightness(5), "stored brighter than empty" + seen);
        }
    }

private:
    // The right-click clip menu: which items are enabled per pad state, what each sends, the confirms, the lock.
    // Kept apart from runTest so no single function grows a large stack frame.
    void runMenuTests()
    {
        beginTest("menu states: empty pad, stored pad, playing pad");
        {
            Rig r;
            r.set({ 0, 2 }, 2);
            const auto empty = r.strip.menuStateFor(5);
            expect(empty.store && ! empty.copy && ! empty.cut && ! empty.clear, "empty pad: only Store here");
            const auto stored = r.strip.menuStateFor(0);
            expect(! stored.store && stored.copy && stored.cut && stored.clear, "stored pad: Copy, Cut, Clear");
            const auto playing = r.strip.menuStateFor(2);
            expect(! playing.store && playing.copy && playing.cut && playing.clear, "the playing pad is a stored pad");
            expect(! r.strip.menuStateFor(-1).store && ! r.strip.menuStateFor(8).copy, "no pad, no items");
        }

        beginTest("menu Paste: enabled with a fitting clip, otherwise disabled with the reason as its tooltip");
        {
            Rig r;
            r.set({ 0 }, -1);
            auto st = r.strip.menuStateFor(0);
            expect(! st.paste && st.pasteLabel == "Paste (nothing copied)", "clipboard empty");
            r.view.clipboardEmpty = false;
            r.view.pasteFits = false;
            st = r.strip.menuStateFor(5);
            expect(! st.paste && st.pasteLabel == "Paste (other layer type)", "clipboard holds another layer type");
            r.view.pasteFits = true;
            expect(r.strip.menuStateFor(5).paste && r.strip.menuStateFor(0).paste, "empty and stored pads both take it");
            expect(r.strip.menuStateFor(5).pasteLabel == "Paste", "plain label when enabled");
        }

        beginTest("menu items send their action; only Clear and Paste over a stored clip ask");
        {
            Rig r;
            r.set({ 0, 2 }, 2);
            r.view.clipboardEmpty = false;

            r.strip.menuChosen(5, ClipStrip::menuStore);
            expect(r.sent(ControlActionType::StoreClip, 5) && r.asked.empty());
            expect(r.lines.contains("Clip|Stored Kick clip 6"));

            r.strip.menuChosen(0, ClipStrip::menuCopy);
            expect(r.sent(ControlActionType::CopyClip, 0) && r.asked.empty());
            expect(r.lines.contains("Clip|Copied Kick clip 1"));

            r.strip.menuChosen(0, ClipStrip::menuCut);
            expect(r.sent(ControlActionType::CutClip, 0) && r.asked.empty(), "Cut does not ask");
            expect(r.lines.contains("Clip|Cut Kick clip 1"));

            r.sink.got.clear();
            r.strip.menuChosen(6, ClipStrip::menuPaste);
            expect(r.sent(ControlActionType::PasteClip, 6) && r.asked.empty(), "Paste onto an empty pad does not ask");
            expect(r.lines.contains("Clip|Pasted Kick clip 7"));

            r.sink.got.clear();
            r.strip.menuChosen(0, ClipStrip::menuPaste);   // stored, not playing
            expect(r.sink.got.empty() && r.asked.size() == 1, "nothing is written before the answer");
            expectEquals(r.asked[0].title, juce::String("Replace Clip"));
            expectEquals(r.asked[0].message, juce::String("Replace clip 1 of \"Kick\" with the copied clip?\nThis cannot be undone."));
            expectEquals(r.asked[0].button, juce::String("Replace"));
            r.asked[0].go();
            expect(r.sent(ControlActionType::PasteClip, 0));

            r.sink.got.clear();
            r.asked.clear();
            r.strip.menuChosen(2, ClipStrip::menuPaste);   // the playing pad asks too (the processor then relaunches it)
            expect(r.asked.size() == 1 && r.asked[0].title == "Replace Clip" && r.sink.got.empty());
            r.asked[0].go();
            expect(r.sent(ControlActionType::PasteClip, 2));

            r.sink.got.clear();
            r.asked.clear();
            r.strip.menuChosen(2, ClipStrip::menuClear);
            expect(r.asked.size() == 1 && r.asked[0].title == "Clear Clip" && r.sink.got.empty());
            r.asked[0].go();
            expect(r.sent(ControlActionType::ClearClip, 2));
            expect(r.lines.contains("Clip|Cleared Kick clip 3"));
        }

        beginTest("menu: a paste the layer would refuse says so and sends nothing; an empty clipboard is silent");
        {
            Rig r;
            r.set({ 0 }, -1);
            r.strip.pastePad(5);
            expect(r.sink.got.empty() && r.asked.empty() && r.lines.isEmpty(), "nothing copied");

            r.view.clipboardEmpty = false;
            r.view.pasteFits = false;
            r.strip.pastePad(5);
            expect(r.sink.got.empty() && r.asked.empty(), "nothing sent");
            expect(r.lines.contains("Clip|Clip 6 is for a different layer type"), r.lines.joinIntoString(" / "));
        }

        beginTest("menu is shut in Performance mode and while a full preset is staged, and a late answer writes nothing");
        {
            Rig r;
            r.set({ 0, 2 }, 2);
            r.view.clipboardEmpty = false;
            r.strip.menuChosen(0, ClipStrip::menuClear);   // the dialog opens
            expectEquals((int) r.asked.size(), 1);

            r.view.fullStaged = true;
            r.view.changed();
            expect(r.strip.isMenuBlocked());
            const auto st = r.strip.menuStateFor(0);
            expect(! st.store && ! st.copy && ! st.cut && ! st.paste && ! st.clear, "every item off");
            r.asked[0].go();
            expect(r.sink.got.empty(), "the staged preset arrived while the dialog was open");
            r.asked.clear();
            for (int id : { (int) ClipStrip::menuStore, (int) ClipStrip::menuCopy, (int) ClipStrip::menuCut,
                            (int) ClipStrip::menuPaste, (int) ClipStrip::menuClear })
                r.strip.menuChosen(id == ClipStrip::menuStore ? 5 : 0, id);
            expect(r.sink.got.empty() && r.asked.empty(), "no item does anything with a full preset staged");

            r.view.fullStaged = false;
            r.view.perf = true;
            r.view.changed();
            expect(r.strip.isMenuBlocked() && ! r.strip.menuStateFor(0).copy);
            r.strip.menuChosen(0, ClipStrip::menuCopy);
            r.strip.menuChosen(0, ClipStrip::menuPaste);
            expect(r.sink.got.empty() && r.asked.empty(), "and none in Performance mode");

            r.view.perf = false;
            r.view.changed();
            r.strip.menuChosen(0, ClipStrip::menuCopy);
            expect(r.sent(ControlActionType::CopyClip, 0), "reopened");
        }
    }
};

static ClipStripTests clipStripTests;
