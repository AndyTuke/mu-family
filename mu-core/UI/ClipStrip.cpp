#include "ClipStrip.h"
#include "UI/ConfirmDialog.h"
#include "MuLimits.h"

static_assert(ClipStrip::kPads == mu_limits::kClipsPerLayer, "the strip shows one pad per stored clip");

ClipStrip::ClipStrip()
{
    lamps.fill(Lamp::Empty);
    flashUntil.fill(0.0);
    askConfirm = [this](const juce::String& title, const juce::String& message, const juce::String& button,
                        std::function<void()> onConfirm)
    {
        mu_ui::confirmAsync(this, title, message, button, std::move(onConfirm));
    };
}

ClipStrip::~ClipStrip()
{
    stopTimer();
    if (view != nullptr) view->removeListener(this);
}

void ClipStrip::bind(const mu_core::LiveView* v, mu_core::ControlSink* s)
{
    if (view != nullptr) view->removeListener(this);
    view = v;
    sink = s;
    if (view != nullptr) view->addListener(this);
    haveBaseline = false;
    refresh();
}

void ClipStrip::setLayer(int layerIndex)
{
    if (layerIndex == layer && haveBaseline) return;
    layer = layerIndex;
    haveBaseline = false;   // a different layer's pads are a new picture, not a change: nothing flashes
    refresh();
}

void ClipStrip::setLayerName(const juce::String& name) { layerName = name; }
void ClipStrip::setColour(juce::Colour c)              { colour = c; repaint(); }
void ClipStrip::setNoun(const juce::String& n)         { noun = n.toLowerCase(); }

bool ClipStrip::isReadOnly() const { return view != nullptr && view->isPerformanceMode(); }

// ── Pure helpers ─────────────────────────────────────────────────────────────

ClipStrip::Lamp ClipStrip::lampFor(int pad, int playing, int queued, uint8_t storedMask, bool layerMuted) noexcept
{
    if (pad == playing) return layerMuted ? Lamp::MutedCurrent : Lamp::Current;
    if (pad == queued)  return Lamp::Queued;
    if (pad >= 0 && pad < kPads && ((storedMask >> pad) & 1) != 0) return Lamp::Available;
    return Lamp::Empty;
}

int ClipStrip::stripWidth()
{
    using mu_ui::s;
    return s(MuLookAndFeel::kClipLabelW) + kPads * s(MuLookAndFeel::kClipPadW) + (kPads - 1) * s(MuLookAndFeel::kClipPadGap);
}

int ClipStrip::widthWithGap() { return stripWidth() + mu_ui::s(MuLookAndFeel::kClipStripGap); }

ClipStrip::Layout ClipStrip::computeLayout(juce::Rectangle<int> bounds)
{
    using mu_ui::s;
    Layout out;
    out.caption = bounds.withWidth(s(MuLookAndFeel::kClipLabelW));
    const int padW = s(MuLookAndFeel::kClipPadW), padH = s(MuLookAndFeel::kClipPadH), gap = s(MuLookAndFeel::kClipPadGap);
    const int y = bounds.getY() + (bounds.getHeight() - padH) / 2;
    for (int i = 0; i < kPads; ++i)
        out.pads[(size_t) i] = { out.caption.getRight() + i * (padW + gap), y, padW, padH };
    return out;
}

int ClipStrip::padAt(const Layout& layout, juce::Point<int> p)
{
    for (int i = 0; i < kPads; ++i)
        if (layout.pads[(size_t) i].contains(p)) return i;
    return -1;
}

// ── Live state ───────────────────────────────────────────────────────────────

bool ClipStrip::storedAt(int pad) const
{
    return view != nullptr && pad >= 0 && pad < kPads && ((view->getClipMask(layer) >> pad) & 1) != 0;
}

bool ClipStrip::playingAt(int pad) const { return view != nullptr && pad >= 0 && view->getPlayingClip(layer) == pad; }

juce::String ClipStrip::clipText(int pad) const { return "clip " + juce::String(pad + 1); }

void ClipStrip::refresh()
{
    const int  playing = view != nullptr ? view->getPlayingClip(layer) : -1;
    const int  queued  = view != nullptr ? view->getQueuedClip(layer) : -1;
    const auto mask    = view != nullptr ? view->getClipMask(layer) : (uint8_t) 0;
    const bool muted   = view != nullptr && view->isLayerMuted(layer);

    std::array<Lamp, kPads> next;
    for (int i = 0; i < kPads; ++i) next[(size_t) i] = lampFor(i, playing, queued, mask, muted);

    // A pad that has just become the playing clip flashes brighter for a moment; the first picture after a
    // (re)bind is a baseline and flashes nothing. A mute change (steady <-> flashing) is not a new clip.
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < kPads; ++i)
    {
        const auto before = lamps[(size_t) i];
        const bool wasPlaying = before == Lamp::Current || before == Lamp::MutedCurrent;
        if (! haveBaseline)
            flashUntil[(size_t) i] = 0.0;
        else if (next[(size_t) i] == Lamp::Current && ! wasPlaying)
            flashUntil[(size_t) i] = now + (double) MuLookAndFeel::lighting().stepFlashMs;
    }
    lamps = next;
    haveBaseline = true;

    // Entering Performance mode cancels any clip menu that is open (a late pick is refused anyway).
    if (isReadOnly()) juce::PopupMenu::dismissAllActiveMenus();

    // The Queued status line stays while a clip waits for its boundary and goes when it lands or is cancelled.
    const juce::String q = queued >= 0 ? layerName + " " + clipText(queued) + " (at the next swap point)" : juce::String();
    if (q != queuedStatus)
    {
        queuedStatus = q;
        ++statusSerial;   // supersedes a pending timed clear
        if (onStatusUpdate) onStatusUpdate(q.isNotEmpty() ? "Queued" : juce::String(), q);
    }

    updateTimer();
    repaint();
}

// The animation timer runs only while a pad pulses, flashes or has just started.
void ClipStrip::updateTimer()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool animated = false;
    for (int i = 0; i < kPads; ++i)
        animated = animated || lamps[(size_t) i] == Lamp::Queued || lamps[(size_t) i] == Lamp::MutedCurrent
                            || flashUntil[(size_t) i] > now;
    if (animated && ! isTimerRunning())      startTimerHz(mu_ui::kUiRefreshHz);
    else if (! animated && isTimerRunning()) stopTimer();
}

// ── Status text ──────────────────────────────────────────────────────────────

void ClipStrip::say(const juce::String& name, const juce::String& value, bool timed)
{
    if (! onStatusUpdate) return;
    onStatusUpdate(name, value);
    if (! timed) return;

    // Clear it after a few seconds, unless a newer line has replaced it.
    const int serial = ++statusSerial;
    juce::Component::SafePointer<ClipStrip> safe(this);
    juce::Timer::callAfterDelay(MuLookAndFeel::kPerfStatusMs, [safe, serial]
    {
        if (auto* strip = safe.getComponent())
            if (strip->statusSerial == serial && strip->onStatusUpdate) strip->onStatusUpdate({}, {});
    });
}

std::pair<juce::String, juce::String> ClipStrip::hoverFor(int pad) const
{
    juce::String state;
    switch (getLamp(pad))
    {
        case Lamp::MutedCurrent: state = "playing, muted"; break;
        case Lamp::Current:      state = "playing"; break;
        case Lamp::Queued:       state = "queued (at the next swap point)"; break;
        case Lamp::Available:
        case Lamp::Fault:        state = "stored"; break;
        case Lamp::Empty:        state = "empty, click to store"; break;
    }
    return { "Clip " + juce::String(pad + 1), layerName + ": " + state };
}

juce::String ClipStrip::getTooltip()
{
    if (isReadOnly()) return {};
    const int pad = padAt(computeLayout(getLocalBounds()), getMouseXYRelative());
    if (pad < 0) return {};
    return storedAt(pad) ? "Play clip " + juce::String(pad + 1) + " (click again to mute)"
                         : "Store this " + noun + " here";
}

// ── Gestures ─────────────────────────────────────────────────────────────────

bool ClipStrip::send(mu_core::ControlActionType type, int pad)
{
    if (sink == nullptr) return false;
    mu_core::ControlAction a;
    a.type  = type;
    a.layer = layer;
    a.index = pad;
    return sink->perform(a);
}

void ClipStrip::confirm(const juce::String& title, const juce::String& message, const juce::String& button,
                        std::function<void()> go)
{
    if (askConfirm) askConfirm(title, message, button, std::move(go));
}

void ClipStrip::clickPad(int pad, bool shift)
{
    if (isReadOnly() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads) return;

    if (shift || ! storedAt(pad))   // Shift stores anywhere; a plain click on an empty pad stores too
    {
        storePad(pad);
        return;
    }

    // A stored pad: the sink launches it, or mutes the playing clip, or cancels a queue behind it.
    const bool wasPlaying = playingAt(pad);
    const bool hadQueue   = view->getQueuedClip(layer) >= 0;
    if (! send(mu_core::ControlActionType::LaunchClip, pad)) return;
    if (wasPlaying && ! hadQueue)
        say("Mute", layerName + (view->isLayerMuted(layer) ? " muted" : " unmuted"), true);
    else if (! wasPlaying && view->getPlayingClip(layer) == pad)   // a stopped transport plays it at once
        say("Clip", "Launched " + layerName + " " + clipText(pad), true);
}

void ClipStrip::storePad(int pad)
{
    if (isReadOnly() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads) return;

    const int          target = layer;
    const juce::String name   = layerName;
    juce::Component::SafePointer<ClipStrip> safe(this);
    auto go = [safe, target, pad, name]
    {
        auto* strip = safe.getComponent();
        if (strip == nullptr || strip->isReadOnly() || strip->layer != target) return;   // the lock or a layer change cancels
        if (strip->send(mu_core::ControlActionType::StoreClip, pad))
            strip->say("Clip", "Stored " + name + " " + strip->clipText(pad), true);
    };

    // Replacing a stored clip that is not the one playing asks first; updating the playing clip after a tweak does not.
    if (storedAt(pad) && ! playingAt(pad))
        confirm("Replace Clip", "Replace " + clipText(pad) + " of \"" + name + "\" with its current state?\nThis cannot be undone.",
                "Replace", std::move(go));
    else
        go();
}

void ClipStrip::clearPad(int pad)
{
    if (isReadOnly() || sink == nullptr || view == nullptr || ! storedAt(pad)) return;

    const int          target = layer;
    const juce::String name   = layerName;
    juce::Component::SafePointer<ClipStrip> safe(this);
    confirm("Clear Clip", "Clear " + clipText(pad) + " of \"" + name + "\"?\nThis cannot be undone.", "Clear",
            [safe, target, pad, name]
            {
                auto* strip = safe.getComponent();
                if (strip == nullptr || strip->isReadOnly() || strip->layer != target) return;
                if (strip->send(mu_core::ControlActionType::ClearClip, pad))
                    strip->say("Clip", "Cleared " + name + " " + strip->clipText(pad), true);
            });
}

void ClipStrip::menuChosen(int pad, int menuResult)
{
    if (menuResult == menuStore)      storePad(pad);
    else if (menuResult == menuClear) clearPad(pad);
}

void ClipStrip::showPopup(int pad)
{
    if (isReadOnly()) return;
    juce::PopupMenu menu;
    menu.addItem(menuStore, "Store here");
    menu.addItem(menuClear, "Clear", storedAt(pad));

    juce::Component::SafePointer<ClipStrip> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                           .withTargetScreenArea(localAreaToGlobal(computeLayout(getLocalBounds()).pads[(size_t) pad])),
                       [safe, pad](int result) { if (safe != nullptr) safe->menuChosen(pad, result); });
}

void ClipStrip::mouseDown(const juce::MouseEvent& e)
{
    const int pad = padAt(computeLayout(getLocalBounds()), e.getPosition());
    if (pad < 0) return;
    if (e.mods.isPopupMenu() || e.mods.isCtrlDown()) showPopup(pad);
    else                                             clickPad(pad, e.mods.isShiftDown());
}

void ClipStrip::mouseMove(const juce::MouseEvent& e)
{
    if (! onStatusUpdate) return;
    const int pad = padAt(computeLayout(getLocalBounds()), e.getPosition());
    if (pad < 0) return;
    const auto h = hoverFor(pad);
    onStatusUpdate(h.first, h.second);
}

// ── Painting ─────────────────────────────────────────────────────────────────

void ClipStrip::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::sf;
    const auto& L = MuLookAndFeel::lighting();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const auto layout = computeLayout(getLocalBounds());

    // The caption: engraved into the metal, or plain text in the flat look.
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawEngravedText(g, "Clip", layout.caption, juce::Justification::centredLeft,
                                        MuLookAndFeel::colour(Id::labelText));
    else
    {
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.drawText("Clip", layout.caption, juce::Justification::centredLeft, true);
    }

    // The pads: a dark base with the lit lens a pixel inside it, lit by the clip's state, its number engraved on top.
    const float corner = sf((float) MuLookAndFeel::kLampCellCorner);
    for (int i = 0; i < kPads; ++i)
    {
        const auto cell  = layout.pads[(size_t) i].toFloat();
        const bool flash = flashUntil[(size_t) i] > now;
        const float lit  = mu_ui::lampLit(lamps[(size_t) i], now, flash);

        g.setColour(MuLookAndFeel::lampBase());
        g.fillRoundedRectangle(cell, corner);
        juce::Path lens;
        lens.addRoundedRectangle(cell.reduced(1.0f), corner);
        MuLookAndFeel::drawLamp(g, lens, {}, MuLookAndFeel::lampColour(colour, lit), cell.getCentre(),
                                juce::jmax(cell.getWidth(), cell.getHeight()) * L.lampHotSpotReach);

        // Muted grey on an unlit or dim lens, the dark base colour once the lens is lit.
        const auto ink = lit >= L.lampOn ? MuLookAndFeel::lampBase() : MuLookAndFeel::colour(Id::mutedText);
        MuLookAndFeel::drawEngravedText(g, juce::String(i + 1), layout.pads[(size_t) i], juce::Justification::centred, ink);
    }
}
