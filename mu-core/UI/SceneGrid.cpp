#include "SceneGrid.h"
#include "UI/Components/PadLamp.h"
#include "MuLimits.h"

static_assert(SceneGrid::kPads == mu_limits::kClipsPerLayer, "one scene per clip index");

SceneGrid::SceneGrid()
{
    lamps.fill(Lamp::Empty);
    stored.fill(0);
    flashUntil.fill(0.0);
}

SceneGrid::~SceneGrid()
{
    stopTimer();
    if (view != nullptr) view->removeListener(this);
}

void SceneGrid::bind(const mu_core::LiveView* v, mu_core::ControlSink* s)
{
    if (view != nullptr) view->removeListener(this);
    view = v;
    sink = s;
    if (view != nullptr) view->addListener(this);
    haveBaseline = false;
    queuedScene  = -1;
    seenSceneSerial = view != nullptr ? view->getLastSceneLaunch().serial : 0;
    refresh();
}

bool SceneGrid::isReadOnly() const { return view != nullptr && view->isPerformanceMode(); }

// ── Pure helpers ─────────────────────────────────────────────────────────────

SceneGrid::Lamp SceneGrid::lampFor(mu_core::SceneState state) noexcept
{
    switch (state)
    {
        case mu_core::SceneState::Queued:  return Lamp::Queued;
        case mu_core::SceneState::Playing: return Lamp::Current;
        case mu_core::SceneState::Stored:  return Lamp::Available;
        case mu_core::SceneState::Empty:   break;
    }
    return Lamp::Empty;
}

juce::String SceneGrid::hoverText(int pad, int layerCount)
{
    const auto scene = "Scene " + juce::String(pad + 1);
    return layerCount > 0 ? scene + ": clip " + juce::String(pad + 1) + " on " + juce::String(layerCount) + (layerCount == 1 ? " layer" : " layers")
                          : scene + ": no layer has clip " + juce::String(pad + 1);
}

juce::String SceneGrid::clickText(int pad, int layerCount)
{
    const auto clip = "Clip " + juce::String(pad + 1);
    return layerCount > 0 ? clip + " on " + juce::String(layerCount) + (layerCount == 1 ? " layer" : " layers")
                          : clip + " is empty";
}

int SceneGrid::gridWidth()
{
    using mu_ui::s;
    return kColumns * s(MuLookAndFeel::kClipPadW) + (kColumns - 1) * s(MuLookAndFeel::kClipPadGap);
}

int SceneGrid::gridHeight()
{
    using mu_ui::s;
    return kRows * s(MuLookAndFeel::kClipPadH) + (kRows - 1) * s(MuLookAndFeel::kClipPadGap);
}

int SceneGrid::blockHeight() { return mu_ui::s(MuLookAndFeel::kClientNameH) + gridHeight(); }

SceneGrid::Layout SceneGrid::computeLayout(juce::Rectangle<int> bounds)
{
    using mu_ui::s;
    Layout out;
    const int capH = s(MuLookAndFeel::kClientNameH);
    out.caption = bounds.withHeight(capH);
    const int padW = s(MuLookAndFeel::kClipPadW), padH = s(MuLookAndFeel::kClipPadH), gap = s(MuLookAndFeel::kClipPadGap);
    const int x0 = bounds.getX() + (bounds.getWidth() - gridWidth()) / 2;
    const int y0 = bounds.getY() + capH;
    for (int i = 0; i < kPads; ++i)   // numbered row by row: 1 2 / 3 4 / 5 6 / 7 8
        out.pads[(size_t) i] = { x0 + (i % kColumns) * (padW + gap), y0 + (i / kColumns) * (padH + gap), padW, padH };
    return out;
}

int SceneGrid::padAt(const Layout& layout, juce::Point<int> p)
{
    for (int i = 0; i < kPads; ++i)
        if (layout.pads[(size_t) i].contains(p)) return i;
    return -1;
}

// ── Live state ───────────────────────────────────────────────────────────────

void SceneGrid::refresh()
{
    // One lamp per clip index, aggregated over every layer (a scene spans all layers, not the shown page).
    std::array<Lamp, kPads> next;
    for (int clip = 0; clip < kPads; ++clip)
    {
        const auto info = view != nullptr ? mu_core::sceneInfo(*view, clip) : mu_core::SceneInfo {};
        next[(size_t) clip]   = lampFor(info.state);
        stored[(size_t) clip] = info.layers;
    }

    // A scene that has just become fully playing flashes brighter for a moment; the first picture after a (re)bind is
    // a baseline and flashes nothing.
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < kPads; ++i)
    {
        if (! haveBaseline)
            flashUntil[(size_t) i] = 0.0;
        else if (next[(size_t) i] == Lamp::Current && lamps[(size_t) i] != Lamp::Current)
            flashUntil[(size_t) i] = now + (double) MuLookAndFeel::lighting().stepFlashMs;
    }
    lamps = next;
    haveBaseline = true;

    // A "Queued" line goes when its scene lands or is cancelled.
    if (queuedScene >= 0 && lamps[(size_t) queuedScene] != Lamp::Queued)
    {
        queuedScene = -1;
        ++statusSerial;   // supersedes a pending timed clear
        if (onStatusUpdate) onStatusUpdate({}, {});
    }

    // A scene was launched, by this grid, the surface or MIDI: say what it did. The first picture after a (re)bind only
    // takes the serial as already reported.
    if (view != nullptr)
    {
        const auto launch = view->getLastSceneLaunch();
        if (launch.serial != seenSceneSerial)
        {
            seenSceneSerial = launch.serial;
            if (haveBaseline && launch.clip >= 0 && launch.clip < kPads)
            {
                if (lamps[(size_t) launch.clip] == Lamp::Queued)   // waiting for its boundary: the line stays until it lands
                {
                    queuedScene = launch.clip;
                    say("Queued", "Scene " + juce::String(launch.clip + 1) + " (at the next swap point)", false);
                }
                else
                    say("Scene", clickText(launch.clip, launch.layers), true);
            }
        }
    }

    updateTimer();
    repaint();
}

// The animation timer runs only while a pad pulses or has just flashed.
void SceneGrid::updateTimer()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool animated = false;
    for (int i = 0; i < kPads; ++i)
        animated = animated || lamps[(size_t) i] == Lamp::Queued || flashUntil[(size_t) i] > now;
    if (animated && ! isTimerRunning())      startTimerHz(mu_ui::kUiRefreshHz);
    else if (! animated && isTimerRunning()) stopTimer();
}

// ── Status text and gestures ─────────────────────────────────────────────────

void SceneGrid::say(const juce::String& name, const juce::String& value, bool timed)
{
    if (! onStatusUpdate) return;
    onStatusUpdate(name, value);
    if (! timed) return;

    // Clear it after a few seconds, unless a newer line has replaced it.
    const int serial = ++statusSerial;
    juce::Component::SafePointer<SceneGrid> safe(this);
    juce::Timer::callAfterDelay(MuLookAndFeel::kPerfStatusMs, [safe, serial]
    {
        if (auto* grid = safe.getComponent())
            if (grid->statusSerial == serial && grid->onStatusUpdate) grid->onStatusUpdate({}, {});
    });
}

void SceneGrid::clickPad(int pad)
{
    if (isReadOnly() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads) return;

    mu_core::ControlAction a;
    a.type  = mu_core::ControlActionType::LaunchScene;
    a.index = pad;
    sink->perform(a);   // the same action the surface sends; an empty scene does nothing. The live state reports it back.
}

juce::String SceneGrid::getTooltip()
{
    if (isReadOnly()) return {};
    return padAt(computeLayout(getLocalBounds()), getMouseXYRelative()) >= 0
               ? "Launch this clip on every layer that has one." : juce::String();
}

void SceneGrid::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isShiftDown() || e.mods.isCtrlDown()) return;   // a plain click only
    clickPad(padAt(computeLayout(getLocalBounds()), e.getPosition()));
}

void SceneGrid::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate) return;
    const int pad = padAt(computeLayout(getLocalBounds()), e.getPosition());
    if (pad < 0) return;
    onStatusUpdate("Scene", hoverText(pad, stored[(size_t) pad]));
}

// ── Painting ─────────────────────────────────────────────────────────────────

void SceneGrid::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    const double now    = juce::Time::getMillisecondCounterHiRes();
    const auto   layout = computeLayout(getLocalBounds());

    // The caption: engraved into the metal, or plain text in the flat look, in the same caption font as "Page" beneath it.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(MuLookAndFeel::kEngravedCaptionFont))));
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawEngravedText(g, "Scene", layout.caption, juce::Justification::centred, MuLookAndFeel::colour(Id::labelText));
    else
    {
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.drawText("Scene", layout.caption, juce::Justification::centred, true);
    }

    // The pads, in the shell accent (a scene belongs to no layer), lit by what every layer says about the clip.
    const auto accent = MuLookAndFeel::appAccent(*this);
    for (int i = 0; i < kPads; ++i)
    {
        const bool flash = flashUntil[(size_t) i] > now;
        mu_ui::drawNumberedPad(g, layout.pads[(size_t) i], i + 1, accent, mu_ui::lampLit(lamps[(size_t) i], now, flash));
    }
}
