#include "ClipStrip.h"
#include "UI/ConfirmDialog.h"
#include "UI/Components/PadLamp.h"
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
    haveDisplay  = false;
    refresh();
}

void ClipStrip::setLayer(int layerIndex)
{
    if (layerIndex == layer && haveBaseline) return;
    layer = layerIndex;
    haveBaseline = false;   // a different layer's pads are a new picture, not a change: nothing flashes
    haveDisplay  = false;
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

ClipStrip::DisplayName ClipStrip::displayNameFor(const mu_core::LiveView& view, int layer)
{
    const int queued = view.getQueuedClip(layer);
    if (queued >= 0)
    {
        const auto name = view.getClipName(layer, queued);
        return { name.isNotEmpty() ? name : "clip " + juce::String(queued + 1), true };
    }
    const int playing = view.getPlayingClip(layer);
    if (playing >= 0)
        return { view.getClipName(layer, playing), false };
    return {};
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

    // Tell the header's preset box what to name, only when that changed (so a file pick is not overridden again).
    if (onDisplayName)
    {
        const DisplayName shown = view != nullptr ? displayNameFor(*view, layer) : DisplayName {};
        if (! haveDisplay || shown != lastDisplay)
        {
            lastDisplay = shown;
            haveDisplay = true;
            onDisplayName(shown.text, shown.queued);
        }
    }

    // Entering Performance mode, or staging a full preset, cancels any clip menu that is open (a late pick is refused anyway).
    if (isMenuBlocked()) juce::PopupMenu::dismissAllActiveMenus();

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

// A stored clip this layer will not take: a typed slot refuses a clip of another layer type, so the pad says so
// instead of offering to play it.
bool ClipStrip::refusedAt(int pad) const
{
    return view != nullptr && storedAt(pad) && ! view->isClipForLayer(layer, pad);
}

juce::String ClipStrip::refusedText(int pad) { return "Clip " + juce::String(pad + 1) + " is for a different layer type"; }

std::pair<juce::String, juce::String> ClipStrip::hoverFor(int pad) const
{
    if (refusedAt(pad)) return { "Clip", refusedText(pad) };
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
    if (refusedAt(pad)) return refusedText(pad);
    return storedAt(pad) ? "Play clip " + juce::String(pad + 1) + " (click again to mute). Right-click for more."
                         : "Store this " + noun + " here. Right-click for more.";
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

void ClipStrip::clickPad(int pad)
{
    if (isReadOnly() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads) return;

    if (! storedAt(pad))   // a plain click on an empty pad stores into it
    {
        storePad(pad);
        return;
    }

    // A clip of another layer type is never launched: nothing is staged and no light changes, the status line says why.
    if (refusedAt(pad))
    {
        say("Clip", refusedText(pad), true);
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

bool ClipStrip::isMenuBlocked() const
{
    return isReadOnly() || (view != nullptr && view->isFullPresetStaged());
}

ClipStrip::MenuState ClipStrip::menuStateFor(int pad) const
{
    MenuState m;
    if (isMenuBlocked() || view == nullptr || pad < 0 || pad >= kPads) return m;
    const bool stored = storedAt(pad);
    m.store = ! stored;
    m.copy = m.cut = m.clear = stored;
    m.paste = view->canPasteClip(layer);
    if (! m.paste)
        m.pasteTip = view->isClipboardEmpty() ? "Nothing copied" : "Clip is for a different layer type";
    return m;
}

// One menu action: ask first when `ask` is set, then send it and say what happened. The lock and the layer are
// checked again when the answer arrives, so a Performance-mode switch or layer change during the dialog writes nothing.
void ClipStrip::runMenuAction(int pad, mu_core::ControlActionType type, const juce::String& verb, bool ask,
                              const juce::String& title, const juce::String& message, const juce::String& button)
{
    const int          target = layer;
    const juce::String name   = layerName;
    juce::Component::SafePointer<ClipStrip> safe(this);
    auto go = [safe, target, pad, name, type, verb]
    {
        auto* strip = safe.getComponent();
        if (strip == nullptr || strip->isMenuBlocked() || strip->layer != target) return;
        if (strip->send(type, pad))
            strip->say("Clip", verb + " " + name + " " + strip->clipText(pad), true);
    };
    if (ask) confirm(title, message, button, std::move(go));
    else     go();
}

void ClipStrip::storePad(int pad)
{
    if (isMenuBlocked() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads || storedAt(pad)) return;
    runMenuAction(pad, mu_core::ControlActionType::StoreClip, "Stored", false, {}, {}, {});
}

void ClipStrip::copyPad(int pad)
{
    if (isMenuBlocked() || sink == nullptr || view == nullptr || ! storedAt(pad)) return;
    runMenuAction(pad, mu_core::ControlActionType::CopyClip, "Copied", false, {}, {}, {});
}

void ClipStrip::cutPad(int pad)
{
    if (isMenuBlocked() || sink == nullptr || view == nullptr || ! storedAt(pad)) return;
    runMenuAction(pad, mu_core::ControlActionType::CutClip, "Cut", false, {}, {}, {});
}

void ClipStrip::pastePad(int pad)
{
    if (isMenuBlocked() || sink == nullptr || view == nullptr || pad < 0 || pad >= kPads) return;
    if (! view->canPasteClip(layer))
    {
        if (! view->isClipboardEmpty()) say("Clip", refusedText(pad), true);   // a clip of another layer type
        return;
    }
    runMenuAction(pad, mu_core::ControlActionType::PasteClip, "Pasted", storedAt(pad), "Replace Clip",
                  "Replace " + clipText(pad) + " of \"" + layerName + "\" with the copied clip?\nThis cannot be undone.", "Replace");
}

void ClipStrip::clearPad(int pad)
{
    if (isMenuBlocked() || sink == nullptr || view == nullptr || ! storedAt(pad)) return;
    runMenuAction(pad, mu_core::ControlActionType::ClearClip, "Cleared", true, "Clear Clip",
                  "Clear " + clipText(pad) + " of \"" + layerName + "\"?\nThis cannot be undone.", "Clear");
}

void ClipStrip::menuChosen(int pad, int menuResult)
{
    switch (menuResult)
    {
        case menuStore: storePad(pad); break;
        case menuCopy:  copyPad(pad);  break;
        case menuCut:   cutPad(pad);   break;
        case menuPaste: pastePad(pad); break;
        case menuClear: clearPad(pad); break;
        default: break;
    }
}

namespace
{
// A greyed menu row that shows a tooltip, drawn by the look so it matches its plain neighbours.
class DisabledItemWithTip : public juce::PopupMenu::CustomComponent, public juce::TooltipClient
{
public:
    DisabledItemWithTip(const juce::String& t, const juce::String& tip) : CustomComponent(false), text(t), tooltip(tip) {}

    void getIdealSize(int& w, int& h) override { getLookAndFeel().getIdealPopupMenuItemSize(text, false, -1, w, h); }
    juce::String getTooltip() override { return tooltip; }
    void paint(juce::Graphics& g) override
    {
        getLookAndFeel().drawPopupMenuItem(g, getLocalBounds(), false, false, false, false, false, text, {}, nullptr, nullptr);
    }

private:
    juce::String text, tooltip;
};
}

void ClipStrip::showPopup(int pad)
{
    if (isMenuBlocked()) return;
    const auto st = menuStateFor(pad);

    juce::PopupMenu menu;
    menu.addItem(menuStore, "Store here", st.store);
    menu.addItem(menuCopy, "Copy", st.copy);
    menu.addItem(menuCut, "Cut", st.cut);
    if (st.paste)
        menu.addItem(menuPaste, "Paste");
    else
    {
        // A greyed Paste explains itself on hover; PopupMenu items carry no tooltip, so this one draws through the look.
        juce::PopupMenu::Item paste("Paste");
        paste.setID(menuPaste).setEnabled(false).setCustomComponent(new DisabledItemWithTip("Paste", st.pasteTip));
        menu.addItem(paste);
    }
    menu.addSeparator();
    menu.addItem(menuClear, "Clear", st.clear);

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
    else                                             clickPad(pad);
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

    // The pads, lit by each clip's state; the lens and number are drawn by the one shared pad function.
    for (int i = 0; i < kPads; ++i)
    {
        const bool flash = flashUntil[(size_t) i] > now;
        mu_ui::drawNumberedPad(g, layout.pads[(size_t) i], i + 1, colour, mu_ui::lampLit(lamps[(size_t) i], now, flash));
    }
}
