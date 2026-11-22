#include "KeyScaleSelector.h"
#include "Audio/MusicalScales.h"
#include "ValueFormat.h"

namespace mu_ui
{
using LF = MuLookAndFeel;

namespace
{
    // Shadow room round the raised box, so the box's drop shadow is not clipped by the popup's own bounds.
    constexpr int kShadowMargin = 8;

    bool isSharp(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

    float textWidth(const juce::Font& f, const juce::String& t)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, t, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    }
}

// ── The floating panel ────────────────────────────────────────────────────────────────────────
// A child of the window's top-level component so it floats over the page without moving any layout.
class KeyScaleSelector::Panel final : public juce::Component
{
public:
    explicit Panel(KeyScaleSelector& o) : owner(o), outside(*this)
    {
        setWantsKeyboardFocus(true);
        // A click anywhere else closes it (the owner's own LCD click toggles it, so that one is left to the owner).
        juce::Desktop::getInstance().addGlobalMouseListener(&outside);
    }
    ~Panel() override { juce::Desktop::getInstance().removeGlobalMouseListener(&outside); }

    // Size from the widest scale name, measured, then position under the LCD (clamped inside the window).
    void place(juce::Component& window)
    {
        const auto font = LF::lcdFont(sf((float) LF::kLcdTextH));
        float widest = 0.0f;
        for (const auto& sc : mu_audio::kScales) widest = juce::jmax(widest, textWidth(font, sc.name));
        colW = (int) std::ceil(widest) + 2 * s(LF::kLcdTextPadX);
        const int gap = s(LF::kSpaceXS), pad = s(LF::kSpaceS), row = s(LF::kDropdownH);
        // Both parts are as wide as the two scale columns; the keyboard's seven naturals share that width.
        const int inner = juce::jmax(2 * colW + gap, owner.getWidth());
        colW = (inner - gap) / 2;
        const int keysH = 2 * row;                       // sharps above, naturals below
        const int listH = 6 * row + 5 * gap;
        const int boxW = inner + 2 * pad, boxH = keysH + gap + listH + 2 * pad;
        const int m = s(kShadowMargin);
        setSize(boxW + 2 * m, boxH + 2 * m);
        auto at = window.getLocalPoint(&owner, juce::Point<int>(0, owner.getHeight() + s(2)));
        at.x = juce::jlimit(0, juce::jmax(0, window.getWidth()  - getWidth()),  at.x - m);
        at.y = juce::jlimit(0, juce::jmax(0, window.getHeight() - getHeight()), at.y - m);
        setTopLeftPosition(at);
    }

    void paint(juce::Graphics& g) override
    {
        const auto box = boxBounds().toFloat();
        LF::drawRaisedSubPanelShadow(g, box);
        LF::drawRaisedSubPanel(g, box, LF::appAccent(owner));

        const auto font = LF::lcdFont(sf((float) LF::kLcdTextH));
        const auto lit  = LF::lcdLitColour(owner);
        const auto& L   = LF::lighting();

        // Key keyboard: seven naturals below, five sharps above each centred on the join of its neighbours.
        for (int pc = 0; pc < 12; ++pc)
        {
            const auto r = keyCell(pc).toFloat();
            const bool on = pc == owner.key;
            LF::drawLcdGlass(g, r, lit, on);
            if (isSharp(pc)) { g.setColour(juce::Colours::black.withAlpha(L.lcdSharpShade)); g.fillRect(r); }
            g.setColour(on ? lit : lit.withAlpha(L.lampDim));
            g.setFont(font);
            g.drawText(mu_fmt::pitchClass(pc), r, juce::Justification::centred);
            LF::drawLcdFront(g, r);
        }
        // Scale list: twelve LCD rows in two columns, table order.
        for (int i = 0; i < mu_audio::kNumScales; ++i)
        {
            const auto r = scaleCell(i).toFloat();
            const bool on = i == owner.scale;
            LF::drawLcdGlass(g, r, lit, on);
            g.setColour(on ? lit : lit.withAlpha(L.lampDim));
            g.setFont(font);
            g.drawText(mu_audio::kScales[(size_t) i].name, r.reduced((float) s(LF::kLcdTextPadX), 0.0f), juce::Justification::centredLeft);
            LF::drawLcdFront(g, r);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { report(e.getPosition(), false); }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (const int k = keyAt(p); k >= 0)         { owner.pickKey(k);   report(p, true); repaint(); }
        else if (const int sc = scaleAt(p); sc >= 0) { owner.pickScale(sc); report(p, true); repaint(); }
    }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k.isKeyCode(juce::KeyPress::escapeKey)) { juce::MessageManager::callAsync([o = juce::Component::SafePointer<KeyScaleSelector>(&owner)] { if (o) o->closePanel(); }); return true; }
        return false;
    }

private:
    juce::Rectangle<int> boxBounds() const { const int m = s(kShadowMargin); return getLocalBounds().reduced(m); }
    juce::Rectangle<int> inner() const { return boxBounds().reduced(s(LF::kSpaceS)); }

    // The seven naturals share the inner width; a sharp is one cell wide, centred on the join of its two neighbours.
    juce::Rectangle<int> keyCell(int pc) const
    {
        static const int natIndex[12]   = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
        const auto in = inner();
        const int row = s(LF::kDropdownH);
        const int w = in.getWidth() / 7;
        if (! isSharp(pc)) return { in.getX() + natIndex[pc] * w, in.getY() + row, w, row };
        const int left = natIndex[pc - 1];
        return { in.getX() + (left + 1) * w - w / 2, in.getY(), w, row };
    }
    juce::Rectangle<int> scaleCell(int i) const
    {
        const auto in = inner();
        const int row = s(LF::kDropdownH), gap = s(LF::kSpaceXS);
        const int col = i / 6, r = i % 6;
        const int y0 = in.getY() + 2 * row + gap;
        return { in.getX() + col * (colW + gap), y0 + r * (row + gap), colW, row };
    }
    int keyAt(juce::Point<int> p) const
    {
        for (int pc = 0; pc < 12; ++pc) if (isSharp(pc) && keyCell(pc).contains(p)) return pc;   // sharps sit over the naturals
        for (int pc = 0; pc < 12; ++pc) if (keyCell(pc).contains(p)) return pc;
        return -1;
    }
    int scaleAt(juce::Point<int> p) const
    {
        for (int i = 0; i < mu_audio::kNumScales; ++i) if (scaleCell(i).contains(p)) return i;
        return -1;
    }
    // Status bar: hover names what the cell would set; a pick names what was set.
    void report(juce::Point<int> p, bool picked)
    {
        if (const int k = keyAt(p); k >= 0)        reportStatus(owner, "Key", mu_fmt::pitchClass(k));
        else if (const int sc = scaleAt(p); sc >= 0) reportStatus(owner, "Scale", mu_audio::kScales[(size_t) sc].name);
        juce::ignoreUnused(picked);
    }
    // Global mouse: any press outside this panel and its owner's LCD closes it.
    struct OutsideClick final : juce::MouseListener
    {
        explicit OutsideClick(Panel& p) : panel(p) {}
        void mouseDown(const juce::MouseEvent& e) override
        {
            auto* c = e.eventComponent;
            if (c != nullptr && (c == &panel || panel.isParentOf(c) || c == &panel.owner)) return;
            juce::MessageManager::callAsync([o = juce::Component::SafePointer<KeyScaleSelector>(&panel.owner)] { if (o) o->closePanel(); });
        }
        Panel& panel;
    };

    KeyScaleSelector& owner;
    OutsideClick outside;
    int colW = 0;
};

// ── KeyScaleSelector ──────────────────────────────────────────────────────────────────────────

KeyScaleSelector::KeyScaleSelector()
{
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

KeyScaleSelector::~KeyScaleSelector() { closePanel(); }

juce::String KeyScaleSelector::phrase(int k, int sc)
{
    return mu_fmt::pitchClass(k) + " " + mu_audio::kScales[(size_t) mu_audio::clampScaleIndex(sc)].name;
}

void KeyScaleSelector::setKey(int k)
{
    k = ((k % 12) + 12) % 12;
    if (k == key) return;
    key = k;
    repaint();
    if (panel) panel->repaint();
}

void KeyScaleSelector::setScale(int sc)
{
    sc = mu_audio::clampScaleIndex(sc);
    if (sc == scale) return;
    scale = sc;
    repaint();
    if (panel) panel->repaint();
}

void KeyScaleSelector::pickKey(int k)
{
    setKey(k);
    if (onKeyChanged) onKeyChanged(key);
}

void KeyScaleSelector::pickScale(int sc)
{
    setScale(sc);
    if (onScaleChanged) onScaleChanged(scale);
}

void KeyScaleSelector::openPanel()
{
    auto* window = getTopLevelComponent();
    if (window == nullptr || panel != nullptr) return;
    panel = std::make_unique<Panel>(*this);
    window->addAndMakeVisible(*panel);
    panel->place(*window);
    panel->toFront(true);
    panel->grabKeyboardFocus();
}

void KeyScaleSelector::closePanel()
{
    if (panel == nullptr) return;
    if (auto* parent = panel->getParentComponent()) parent->removeChildComponent(panel.get());
    panel.reset();
}

void KeyScaleSelector::parentHierarchyChanged()
{
    if (! isShowing()) closePanel();
}

void KeyScaleSelector::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto lit = LF::lcdLitColour(*this);
    const bool on = isEnabled();
    LF::drawLcdGlass(g, r, lit, false);
    if (isMouseOver() && on) { g.setColour(lit.withAlpha(LF::lighting().lampDim * 0.25f)); g.fillRect(r); }

    // The phrase, left-padded like the family dropdown, the arrow at the right.
    const float arrow = r.getHeight() * 0.3f;
    const float ax = r.getWidth() - arrow * 1.7f, ay = (r.getHeight() - arrow * 0.6f) * 0.5f;
    juce::Path tri;
    tri.addTriangle(ax, ay, ax + arrow, ay, ax + arrow * 0.5f, ay + arrow * 0.6f);
    const float strength = on ? 1.0f : LF::lighting().lampDim;
    g.setColour(lit.withMultipliedAlpha(strength));
    g.fillPath(tri);
    g.setFont(LF::lcdFont(sf((float) LF::kLcdTextH)));
    g.drawText(phrase(key, scale), r.withTrimmedLeft((float) s(LF::kLcdTextPadX)).withTrimmedRight(arrow * 2.0f),
               juce::Justification::centredLeft, true);
    LF::drawLcdFront(g, r);
}

void KeyScaleSelector::mouseEnter(const juce::MouseEvent&)
{
    if (isEnabled()) reportStatus(*this, "Key and scale", phrase(key, scale));
}

void KeyScaleSelector::mouseDown(const juce::MouseEvent&)
{
    if (! isEnabled()) return;
    if (panel) closePanel(); else openPanel();
    reportStatus(*this, "Key and scale", phrase(key, scale));
}

void KeyScaleSelector::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! isEnabled() || panel != nullptr || w.deltaY == 0.0f) return;
    const int next = juce::jlimit(0, mu_audio::kNumScales - 1, scale + (w.deltaY > 0.0f ? -1 : 1));
    if (next == scale) return;
    pickScale(next);
    reportStatus(*this, "Scale", mu_audio::kScales[(size_t) scale].name);
}

// ── KeyScalePane ──────────────────────────────────────────────────────────────────────────────

void KeyScalePane::resized()
{
    auto r = getLocalBounds().reduced(s(LF::kSpaceS), 0);
    const int h = s(LF::kDropdownH);
    r = r.withSizeKeepingCentre(r.getWidth(), h);
    captionR = r.removeFromLeft(s(LF::kDropdownCaptionW));
    r.removeFromLeft(s(LF::kSpaceXS));
    selector.setBounds(r);
}

void KeyScalePane::paint(juce::Graphics& g)
{
    if (LF::isMetal(*this))
        LF::drawEngravedText(g, "Key", captionR, juce::Justification::centredLeft, LF::colour(LF::labelText));
    else
    {
        g.setColour(LF::colour(LF::labelText));
        g.setFont(LF::uiFont(sf(LF::kEngravedCaptionFont)));
        g.drawText("Key", captionR, juce::Justification::centredLeft);
    }
}

// ── KeyScaleAttachment ────────────────────────────────────────────────────────────────────────

int KeyScaleAttachment::readIndex(juce::RangedAudioParameter* p)
{
    return p == nullptr ? 0 : (int) std::lround(p->convertFrom0to1(p->getValue()));
}

void KeyScaleAttachment::writeIndex(juce::RangedAudioParameter* p, int index)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1((float) index));
    p->endChangeGesture();
}

KeyScaleAttachment::KeyScaleAttachment(juce::AudioProcessorValueTreeState& apvts, const juce::String& k, const juce::String& sc,
                                       KeyScaleSelector& selector)
    : state(apvts), keyId(k), scaleId(sc), sel(selector)
{
    state.addParameterListener(keyId, this);
    state.addParameterListener(scaleId, this);
    handleAsyncUpdate();
    sel.onKeyChanged   = [this](int v) { writeIndex(state.getParameter(keyId), v); };
    sel.onScaleChanged = [this](int v) { writeIndex(state.getParameter(scaleId), v); };
}

KeyScaleAttachment::~KeyScaleAttachment()
{
    cancelPendingUpdate();
    sel.onKeyChanged = nullptr;
    sel.onScaleChanged = nullptr;
    state.removeParameterListener(keyId, this);
    state.removeParameterListener(scaleId, this);
}

void KeyScaleAttachment::handleAsyncUpdate()
{
    sel.setKey(readIndex(state.getParameter(keyId)));
    sel.setScale(readIndex(state.getParameter(scaleId)));
}

} // namespace mu_ui
