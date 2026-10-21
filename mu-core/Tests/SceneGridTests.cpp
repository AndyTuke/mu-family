// The scene pads (docs/design-ui-family.md 13.9): the aggregate over all layers that lights them, the lamp mapping, the
// geometry (two columns by four rows of the clip strip's own pad), the press that sends LaunchScene, the status lines that
// follow every scene launch wherever it came from, read-only under Performance mode, and the sidebar's opt-in
// (off by default, the layer items still clear of it when on). Driven with a fake LiveView and ControlSink.

#include <juce_gui_basics/juce_gui_basics.h>
#include "LayerContractFixtures.h"
#include "Control/SceneState.h"
#include "UI/LayerSidebar.h"
#include "UI/SceneGrid.h"

using namespace mu_contract_test;

namespace
{
using namespace mu_core;
using Lamp = SceneGrid::Lamp;

struct ScaleGuard
{
    float saved = mu_ui::scale;
    explicit ScaleGuard(float s) { mu_ui::scale = s; }
    ~ScaleGuard() { mu_ui::scale = saved; }
};

// A LiveView with settable clip state for four layers.
struct SceneView : LiveView
{
    static constexpr int kLayers = 4;
    bool perf = false;
    int playing[kLayers] { -1, -1, -1, -1 }, queued[kLayers] { -1, -1, -1, -1 };
    uint8_t mask[kLayers] {}, foreign[kLayers] {};
    SceneLaunch launch;
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
    bool isLayerMuted(int) const override { return false; }
    bool isClipForLayer(int l, int c) const override { return l < 0 || l >= kLayers || ((foreign[l] >> c) & 1) == 0; }
    SceneLaunch getLastSceneLaunch() const override { return launch; }
    void addListener(Listener* l) const override { listeners.add(l); }
    void removeListener(Listener* l) const override { listeners.remove(l); }
    void changed() { listeners.call([](Listener& l) { l.liveStateChanged(); }); }
    void launched(int clip, int layers) { launch = { clip, layers, launch.serial + 1 }; changed(); }
};

struct SceneSink : ControlSink
{
    std::vector<ControlAction> got;
    bool perform(const ControlAction& a) override { got.push_back(a); return true; }
};

struct Rig
{
    SceneView view;
    SceneSink sink;
    SceneGrid grid;
    juce::StringArray lines;

    Rig()
    {
        grid.bind(&view, &sink);
        grid.setBounds(0, 0, 60, SceneGrid::blockHeight());
        grid.onStatusUpdate = [this](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
    }
};
}

class SceneGridTests : public juce::UnitTest
{
public:
    SceneGridTests() : juce::UnitTest("Scene pads: the aggregate, the lamps, the press and the status line", "mu-core") {}

    void runTest() override
    {
        beginTest("a scene is lit from every layer: empty, stored, playing, queued");
        {
            using S = SceneState;
            auto info = [](std::initializer_list<SceneLayerClip> layers)
            {
                std::vector<SceneLayerClip> v(layers);
                return sceneInfoFor(v.data(), (int) v.size());
            };
            expect(info({}).state == S::Empty && info({}).layers == 0, "no layers");
            expect(info({ { false, false, false }, { false, false, false } }).state == S::Empty, "nobody stores it");
            expect(info({ { true, false, false }, { false, false, false } }).state == S::Stored, "stored, not playing");
            expect(info({ { true, true, false }, { true, false, false } }).state == S::Stored, "one plays it, another that stores it does not");
            expect(info({ { true, true, false }, { true, true, false }, { false, false, false } }).state == S::Playing,
                   "every layer that stores it plays it; a layer without it is not counted");
            expect(info({ { true, true, false }, { true, false, true } }).state == S::Queued, "any layer waiting on it");
            expect(info({ { true, true, false }, { true, true, false } }).layers == 2);
            expect(info({ { true, true, false }, { false, false, false }, { true, false, false } }).layers == 2, "counts the layers that store it");
        }

        beginTest("the lamp mapping: empty off, stored dim, playing lit, queued pulsing; no muted state");
        expect(SceneGrid::lampFor(SceneState::Empty) == Lamp::Empty);
        expect(SceneGrid::lampFor(SceneState::Stored) == Lamp::Available);
        expect(SceneGrid::lampFor(SceneState::Playing) == Lamp::Current);
        expect(SceneGrid::lampFor(SceneState::Queued) == Lamp::Queued);

        beginTest("the pads follow the live state across all layers, and a clip of another layer type does not count");
        {
            Rig r;
            r.view.mask[0] = 0b0101; r.view.playing[0] = 0;   // layer 1: clips 1 and 3, playing clip 1
            r.view.mask[1] = 0b0100;                           // layer 2: clip 3
            r.view.mask[3] = 0b1000; r.view.queued[3] = 3;     // layer 4: clip 4 waiting (the last layer, off any "shown page")
            r.view.changed();
            expect(r.grid.getLamp(0) == Lamp::Current, "scene 1: its one layer plays it");
            expect(r.grid.getLamp(1) == Lamp::Empty, "scene 2: nobody stores it");
            expect(r.grid.getLamp(2) == Lamp::Available, "scene 3: two layers store it, one plays");
            expect(r.grid.getLamp(3) == Lamp::Queued, "scene 4: a layer waits on it");
            expectEquals(r.grid.getLayerCount(2), 2);
            expectEquals(r.grid.getLayerCount(1), 0);

            r.view.foreign[1] = 0b0100;   // layer 2's clip 3 is another type's: it would not launch
            r.view.changed();
            expectEquals(r.grid.getLayerCount(2), 1);
            r.view.playing[0] = 2;
            r.view.changed();
            expect(r.grid.getLamp(2) == Lamp::Current, "now the one layer that would take it plays it");
        }

        beginTest("the status texts: hover and click, singular and plural");
        expectEquals(SceneGrid::hoverText(2, 9), juce::String("Scene 3: clip 3 on 9 layers"));
        expectEquals(SceneGrid::hoverText(0, 1), juce::String("Scene 1: clip 1 on 1 layer"));
        expectEquals(SceneGrid::hoverText(4, 0), juce::String("Scene 5: no layer has clip 5"));
        expectEquals(SceneGrid::clickText(2, 9), juce::String("Clip 3 on 9 layers"));
        expectEquals(SceneGrid::clickText(6, 0), juce::String("Clip 7 is empty"));

        beginTest("geometry: two columns by four rows of the clip pad, numbered row by row, centred under the caption");
        for (const float scale : { 1.0f, 1.25f })
        {
            ScaleGuard g(scale);
            using LF = MuLookAndFeel;
            const auto bounds = juce::Rectangle<int>(10, 20, 60, SceneGrid::blockHeight());
            const auto layout = SceneGrid::computeLayout(bounds);
            const int padW = mu_ui::s(LF::kClipPadW), padH = mu_ui::s(LF::kClipPadH), gap = mu_ui::s(LF::kClipPadGap);
            expectEquals(layout.caption.getHeight(), mu_ui::s(LF::kClientNameH));
            expectEquals(layout.caption.getY(), bounds.getY());
            for (int i = 0; i < SceneGrid::kPads; ++i)
            {
                const auto p = layout.pads[(size_t) i];
                expectEquals(p.getWidth(), padW);
                expectEquals(p.getHeight(), padH);
                expectEquals(p.getX(), layout.pads[(size_t) (i % 2)].getX(), "two columns");
                expectEquals(p.getY(), layout.pads[0].getY() + (i / 2) * (padH + gap), "four rows, row by row");
            }
            expectEquals(layout.pads[1].getX() - layout.pads[0].getRight(), gap);
            expect(layout.pads[0].getY() >= layout.caption.getBottom(), "the grid sits under the caption");
            const int left = layout.pads[0].getX() - bounds.getX(), right = bounds.getRight() - layout.pads[1].getRight();
            expect(std::abs(left - right) <= 1, "centred");
            expectEquals(SceneGrid::gridHeight(), 4 * padH + 3 * gap);
            expectEquals(SceneGrid::blockHeight(), mu_ui::s(LF::kClientNameH) + SceneGrid::gridHeight());
            expectEquals(SceneGrid::padAt(layout, layout.pads[5].getCentre()), 5);
            expectEquals(SceneGrid::padAt(layout, { bounds.getX(), bounds.getY() }), -1, "the caption is not a pad");
        }
        {
            // At Medium the block is the ruled 18 + 89.
            ScaleGuard g(1.0f);
            expectEquals(SceneGrid::gridHeight(), 89);
            expectEquals(SceneGrid::blockHeight(), 107);
        }

        beginTest("a press sends LaunchScene(n - 1), the same action the surface sends, and nothing else");
        {
            Rig r;
            r.view.mask[0] = 0b0100;
            r.grid.clickPad(2);
            expectEquals((int) r.sink.got.size(), 1);
            if (! r.sink.got.empty())
            {
                expect(r.sink.got[0].type == ControlActionType::LaunchScene);
                expectEquals(r.sink.got[0].index, 2);
            }
            r.grid.clickPad(-1);
            r.grid.clickPad(SceneGrid::kPads);
            expectEquals((int) r.sink.got.size(), 1, "out of range does nothing");
        }

        beginTest("under Performance mode a press does nothing (the pads follow the surface)");
        {
            Rig r;
            r.view.perf = true;
            expect(r.grid.isReadOnly());
            r.grid.clickPad(1);
            expect(r.sink.got.empty());
            expectEquals(r.grid.getTooltip(), juce::String(), "and no tooltip");
            SceneGrid unbound;
            unbound.clickPad(0);   // no sink, no view: nothing to do and nothing to crash
        }

        beginTest("the status line follows a scene launch, whoever sent it; a queued scene's line stays until it lands");
        {
            Rig r;
            r.view.mask[0] = 0b0100; r.view.mask[1] = 0b0100;
            r.view.changed();
            r.lines.clear();
            r.view.playing[0] = 2; r.view.playing[1] = 2;
            r.view.launched(2, 2);
            expect(r.lines.contains("Scene|Clip 3 on 2 layers"), r.lines.joinIntoString(" / "));

            r.lines.clear();
            r.view.playing[0] = 0; r.view.playing[1] = 0;
            r.view.queued[0] = 2; r.view.queued[1] = 2;
            r.view.launched(2, 2);
            expect(r.lines.contains("Queued|Scene 3 (at the next swap point)"), "waiting: " + r.lines.joinIntoString(" / "));
            expect(! r.lines.contains("Scene|Clip 3 on 2 layers"));

            r.lines.clear();
            r.view.queued[0] = r.view.queued[1] = -1;   // it landed
            r.view.playing[0] = r.view.playing[1] = 2;
            r.view.changed();
            expect(r.lines.contains("|"), "the Queued line clears when the scene lands: " + r.lines.joinIntoString(" / "));

            r.lines.clear();
            r.view.launched(5, 0);
            expect(r.lines.contains("Scene|Clip 6 is empty"), "an empty scene says so: " + r.lines.joinIntoString(" / "));
        }

        beginTest("a launch that was already reported when the grid bound is not announced again");
        {
            SceneView view;
            SceneSink sink;
            view.launch = { 3, 2, 7 };
            SceneGrid grid;
            juce::StringArray lines;
            grid.onStatusUpdate = [&](const juce::String& n, const juce::String& v) { lines.add(n + "|" + v); };
            grid.bind(&view, &sink);
            expect(lines.isEmpty());
        }

        beginTest("the sidebar shows no scenes by default; turned on, the grid sits at the foot and the layer items clear it");
        {
            auto box = std::make_unique<FakeLayerHost>(mu_limits::kMaxLayers < 8 ? mu_limits::kMaxLayers : 8); auto& p = *box;
            LayerSidebar sidebar(p, "Layer");
            sidebar.setBounds(0, 0, LayerSidebar::kWidth, 814);
            sidebar.refreshItems();
            expect(! sidebar.getShowScenes() && ! sidebar.getSceneGrid().isVisible(), "off by default");

            sidebar.setShowScenes(true);
            expect(sidebar.getShowScenes() && sidebar.getSceneGrid().isVisible());
            const auto grid = sidebar.getSceneGrid().getBounds();
            expectEquals(grid.getHeight(), SceneGrid::blockHeight());
            expect(grid.getBottom() <= sidebar.getHeight(), "inside the sidebar");
            expect(grid.getWidth() <= sidebar.getWidth());
            if (sidebar.getNumPages() > 1)
                expect(grid.getBottom() <= sidebar.getPageSwitch().getY(), "above the page switch");

            // The item area (the sidebar's viewport) ends before the scene block starts, and still holds every item.
            juce::Viewport* items = nullptr;
            for (int i = 0; i < sidebar.getNumChildComponents(); ++i)
                if (auto* v = dynamic_cast<juce::Viewport*>(sidebar.getChildComponent(i))) items = v;
            expect(items != nullptr);
            if (items != nullptr)
            {
                expect(items->getBottom() <= grid.getY(), "the items clear the scene block");
                const int shown = juce::jmin(mu_limits::kLayersPerPage, p.getNumLayers());
                expect(items->getHeight() >= shown * mu_ui::s(80), "and there is still room for every item of the page at Medium");
            }

            // The cost the design accepts: the items' gap shrinks. They must not touch, and the gap is reported for the picture.
            const int gapWith = sidebar.getItemGap();
            sidebar.setShowScenes(false);
            const int gapWithout = sidebar.getItemGap();
            logMessage("item gap at Medium (814 px): " + juce::String(gapWithout) + " px without the scene block, " + juce::String(gapWith) + " px with it");
            expect(gapWith >= 1, "the layer items do not touch with the scene block on");
            expect(gapWith < gapWithout, "the block took room from the gaps");
            expect(! sidebar.getSceneGrid().isVisible(), "and off again");
        }
    }
};

static SceneGridTests sSceneGridTests;
