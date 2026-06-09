#include "ModalDialog.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/ModalCard.h"                                   // shared dim + card chrome
#include <juce_audio_processors/juce_audio_processors.h>   // AudioProcessorEditor (host target)

#include <cmath>

namespace mu_ui {

using Id = MuLookAndFeel::ColourIds;

namespace {
    // Depth-first search for an AudioProcessorEditor at or below `c`.
    juce::Component* findEditorDescendant(juce::Component* c)
    {
        if (c == nullptr) return nullptr;
        if (dynamic_cast<juce::AudioProcessorEditor*>(c) != nullptr) return c;
        for (auto* child : c->getChildren())
            if (auto* r = findEditorDescendant(child)) return r;
        return nullptr;
    }

    // Width of `text` in `font` (non-deprecated measure via GlyphArrangement).
    int textWidth(const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        return (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth());
    }

    // Glyph + accent colour for each icon kind (empty = no icon).
    juce::String iconGlyph(ModalDialog::Icon i)
    {
        switch (i)
        {
            case ModalDialog::Icon::Warning:  return juce::String::fromUTF8("\xe2\x9a\xa0"); // ⚠
            case ModalDialog::Icon::Question: return "?";
            case ModalDialog::Icon::Info:     return juce::String::fromUTF8("\xe2\x93\x98"); // ⓘ
            default:                          return {};
        }
    }

    // Build the message body as an AttributedString so measure + draw stay identical.
    juce::AttributedString messageAttr(const juce::String& text)
    {
        juce::AttributedString as;
        as.setText(text);
        as.setJustification(juce::Justification::topLeft);
        as.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(13.0f))));
        as.setColour(MuLookAndFeel::colour(Id::headingText).withMultipliedAlpha(0.85f));
        return as;
    }
}

ModalDialog::ModalDialog()
{
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);
}

ModalDialog::~ModalDialog() = default;

ModalDialog& ModalDialog::title  (const juce::String& t) { titleText = t;   return *this; }
ModalDialog& ModalDialog::message(const juce::String& m) { messageText = m; return *this; }
ModalDialog& ModalDialog::icon   (Icon i)                { iconType = i;    return *this; }

ModalDialog& ModalDialog::button(const juce::String& label, int result, bool primary)
{
    if (primary && defaultResultId == std::numeric_limits<int>::min())
        defaultResultId = result;
    buttonSpecs.push_back({ label, result, primary });
    return *this;
}

ModalDialog& ModalDialog::content(std::unique_ptr<juce::Component> c, int height)
{
    contentComp   = std::move(c);
    contentHeight = height;
    if (contentComp) addAndMakeVisible(*contentComp);
    return *this;
}

int ModalDialog::measureMessageHeight(int contentWidth) const
{
    if (messageText.isEmpty() || contentWidth <= 0) return 0;
    juce::TextLayout tl;
    tl.createLayout(messageAttr(messageText), (float) contentWidth);
    return (int) std::ceil(tl.getHeight());
}

int ModalDialog::computeCardHeight() const
{
    using mu_ui::s;
    const int pad     = s(20);
    const int contentW = s(cardW) - 2 * pad;

    int h = pad + s(30) + s(6);                       // top pad + header + gap
    if (messageText.isNotEmpty()) h += measureMessageHeight(contentW) + s(10);
    if (contentComp)              h += s(contentHeight) + s(12);
    h += s(28) + pad;                                 // button row + bottom pad
    return h;
}

juce::Rectangle<int> ModalDialog::cardBounds() const
{
    using mu_ui::s;
    const int w = s(cardW);
    const int h = computeCardHeight();
    return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
}

void ModalDialog::show(juce::Component* anchor)
{
    // Host over the plugin editor so a DAW dims the editor (not its window). Prefer an
    // editor ancestor of the anchor; else (anchor is e.g. a standalone window) search the
    // top-level's descendants for the editor; else fall back to the top-level itself.
    juce::Component* host = nullptr;
    for (auto* c = anchor; c != nullptr; c = c->getParentComponent())
        if (dynamic_cast<juce::AudioProcessorEditor*>(c) != nullptr) { host = c; break; }
    if (host == nullptr && anchor != nullptr)
    {
        auto* top = anchor->getTopLevelComponent();
        host = findEditorDescendant(top);
        if (host == nullptr) host = top;
    }
    if (host == nullptr) { delete this; return; }

    // Realise the buttons from their specs.
    for (const auto& spec : buttonSpecs)
    {
        auto b = std::make_unique<juce::TextButton>(spec.label);
        if (spec.primary)
        {
            // MuLookAndFeel styles buttons by toggle state, not buttonColourId — give the
            // primary the "active" look (accent bg/border/text) so the default action stands out.
            b->setClickingTogglesState(false);
            b->setToggleState(true, juce::dontSendNotification);
        }
        const int r = spec.result;
        b->onClick = [this, r] { finish(r); };
        addAndMakeVisible(*b);
        buttons.push_back(std::move(b));
    }

    host->addAndMakeVisible(this);
    setBounds(host->getLocalBounds());
    setAlwaysOnTop(true);
    toFront(true);
    if (contentComp) contentComp->grabKeyboardFocus();
    else             grabKeyboardFocus();
}

void ModalDialog::finish(int result)
{
    if (finished) return;
    finished = true;

    juce::Component::SafePointer<ModalDialog> self(this);
    if (auto* p = getParentComponent()) p->removeChildComponent(this);
    if (onResult) onResult(result);
    // Defer the delete: finish() is typically called from inside a child button's event.
    juce::MessageManager::callAsync([self]
    {
        if (auto* d = self.getComponent()) delete d;
    });
}

void ModalDialog::parentSizeChanged()
{
    if (auto* p = getParentComponent()) setBounds(p->getLocalBounds());
}

void ModalDialog::mouseDown(const juce::MouseEvent& e)
{
    if (! cardBounds().contains(e.getPosition()))
        finish(cancelResultId);
}

bool ModalDialog::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { finish(cancelResultId); return true; }
    if (key == juce::KeyPress::returnKey && defaultResultId != std::numeric_limits<int>::min())
    {
        finish(defaultResultId);
        return true;
    }
    return false;
}

void ModalDialog::resized()
{
    using mu_ui::s;
    const auto card = cardBounds();
    const int pad   = s(20);
    const auto inner = card.reduced(pad);

    int y = inner.getY() + s(30) + s(6);              // below header
    if (messageText.isNotEmpty()) y += measureMessageHeight(inner.getWidth()) + s(10);

    if (contentComp)
    {
        contentComp->setBounds(inner.getX(), y, inner.getWidth(), s(contentHeight));
        y += s(contentHeight) + s(12);
    }

    // Buttons: right-aligned, laid out far-right first so the last-added (primary) sits
    // at the right edge.
    const int btnH = s(28);
    const int gap  = s(8);
    const int by   = card.getBottom() - pad - btnH;
    int bx = inner.getRight();
    const juce::Font f(juce::FontOptions{}.withHeight(mu_ui::sf(13.0f)));
    for (int i = (int) buttons.size() - 1; i >= 0; --i)
    {
        const int bw = juce::jmax(s(72), textWidth(f, buttonSpecs[(size_t) i].label) + s(28));
        buttons[(size_t) i]->setBounds(bx - bw, by, bw, btnH);
        bx -= bw + gap;
    }
}

void ModalDialog::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;

    fillModalDim(g);

    const auto card = cardBounds();
    paintModalCard(g, card);

    const int pad = s(20);
    const auto inner = card.reduced(pad);

    // Header: optional icon glyph + title.
    int titleX = inner.getX();
    const auto glyph = iconGlyph(iconType);
    if (glyph.isNotEmpty())
    {
        const auto accent = iconType == Icon::Warning
            ? MuLookAndFeel::colour(Id::knobLevel)            // amber for warnings
            : MuLookAndFeel::colour(Id::globalAccent);
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(20.0f)).withStyle("Bold")));
        g.drawText(glyph, inner.getX(), inner.getY(), s(26), s(26), juce::Justification::centredLeft, false);
        titleX += s(32);
    }
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(15.0f)).withStyle("Bold")));
    g.drawText(titleText, titleX, inner.getY(), inner.getRight() - titleX, s(26),
               juce::Justification::centredLeft, false);

    // Message body (wrapped) below the header.
    if (messageText.isNotEmpty())
    {
        const int msgY = inner.getY() + s(30) + s(6);
        const int msgH = measureMessageHeight(inner.getWidth());
        juce::TextLayout tl;
        tl.createLayout(messageAttr(messageText), (float) inner.getWidth());
        tl.draw(g, juce::Rectangle<int>(inner.getX(), msgY, inner.getWidth(), msgH).toFloat());
    }
}

} // namespace mu_ui
