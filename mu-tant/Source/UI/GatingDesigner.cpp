#include "GatingDesigner.h"

#include <cmath>
#include <thread>

namespace mu_tant
{

namespace
{
    struct SubdivEntry { int denom; const char* label; };
    constexpr SubdivEntry kSubdivOptions[] = {
        { 4,  "1/4"  }, { 8,  "1/8"  }, { 16, "1/16" }, { 32, "1/32" },
    };
    constexpr int kSubdivCount = (int)(sizeof(kSubdivOptions) / sizeof(kSubdivOptions[0]));

    int idForDenom(int d)
    {
        for (int i = 0; i < kSubdivCount; ++i)
            if (kSubdivOptions[i].denom == d) return i + 1;
        return 3;
    }
}

// ── GateToolButton ────────────────────────────────────────────────────────────
GateToolButton::GateToolButton(GateTool t)
    : juce::Button(juce::String()), toolId(t)
{
    setClickingTogglesState(true);
    setRadioGroupId(0x6a7e);
}

void GateToolButton::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    using Id = MuLookAndFeel::ColourIds;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(on ? MuLookAndFeel::colour(Id::segmentActiveBg)
                   : (highlighted ? MuLookAndFeel::colour(Id::segmentInactiveBg).brighter(0.1f)
                                  : MuLookAndFeel::colour(Id::segmentInactiveBg)));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);
    const auto ink = on ? MuLookAndFeel::colour(Id::textBright)
                        : MuLookAndFeel::colour(Id::labelText);
    g.setColour(ink);
    auto b = r.reduced(r.getWidth() * 0.22f);

    switch (toolId)
    {
        case GateTool::Arrow:
        {
            // Pointer arrow
            juce::Path p;
            p.startNewSubPath(b.getX(), b.getY());
            p.lineTo(b.getX(), b.getBottom() - b.getHeight() * 0.25f);
            p.lineTo(b.getX() + b.getWidth() * 0.32f,  b.getCentreY() + b.getHeight() * 0.1f);
            p.lineTo(b.getX() + b.getWidth() * 0.55f,  b.getBottom());
            p.lineTo(b.getRight(),                      b.getBottom() - b.getHeight() * 0.3f);
            p.lineTo(b.getX() + b.getWidth() * 0.6f,   b.getCentreY() - b.getHeight() * 0.05f);
            p.lineTo(b.getRight() - b.getWidth() * 0.1f, b.getY());
            p.closeSubPath();
            g.fillPath(p);
            break;
        }
        case GateTool::Pencil:
        {
            juce::Path p;
            p.startNewSubPath(b.getX(), b.getBottom());
            p.lineTo(b.getRight(), b.getY());
            g.strokePath(p, juce::PathStrokeType(1.6f));
            juce::Path tip;
            tip.addTriangle(b.getX(), b.getBottom(),
                            b.getX() + b.getWidth() * 0.28f, b.getBottom() - b.getHeight() * 0.12f,
                            b.getX() + b.getWidth() * 0.12f, b.getBottom() - b.getHeight() * 0.28f);
            g.fillPath(tip);
            break;
        }
        case GateTool::Eraser:
        {
            juce::Path p;
            p.startNewSubPath(b.getX() + b.getWidth() * 0.2f, b.getBottom());
            p.lineTo(b.getRight(), b.getBottom());
            p.lineTo(b.getRight() - b.getWidth() * 0.2f, b.getY());
            p.lineTo(b.getX(), b.getY());
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.4f));
            break;
        }
        case GateTool::Glue:
        {
            const float cx = b.getCentreX(), cy = b.getCentreY() + b.getHeight() * 0.12f;
            const float rad = b.getWidth() * 0.32f;
            g.fillEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            juce::Path drop;
            drop.addTriangle(cx, b.getY(), cx - rad * 0.7f, cy, cx + rad * 0.7f, cy);
            g.fillPath(drop);
            break;
        }
        case GateTool::Reverse:
        {
            const float midY = b.getCentreY(), q = b.getHeight() * 0.22f;
            juce::Path p;
            p.startNewSubPath(b.getX(), midY - q); p.lineTo(b.getRight(), midY - q);
            p.startNewSubPath(b.getRight(), midY + q); p.lineTo(b.getX(), midY + q);
            g.strokePath(p, juce::PathStrokeType(1.4f));
            juce::Path heads;
            heads.addTriangle(b.getRight(), midY - q, b.getRight() - q, midY - q - q * 0.6f, b.getRight() - q, midY - q + q * 0.6f);
            heads.addTriangle(b.getX(), midY + q, b.getX() + q, midY + q - q * 0.6f, b.getX() + q, midY + q + q * 0.6f);
            g.fillPath(heads);
            break;
        }
    }
}

// ── Constructor ───────────────────────────────────────────────────────────────
GatingDesigner::GatingDesigner()
{
    using Id = MuLookAndFeel::ColourIds;

    // ── Layer toggles ────────────────────────────────────────────────────────
    gaterLayerBtn.setClickingTogglesState(true);
    gaterLayerBtn.setRadioGroupId(0x6a8a);
    gaterLayerBtn.setToggleState(true, juce::dontSendNotification);
    gaterLayerBtn.onClick = [this] { if (currentLayer != GateLayer::Gater)  setLayer(GateLayer::Gater);  };
    filterLayerBtn.setClickingTogglesState(true);
    filterLayerBtn.setRadioGroupId(0x6a8a);
    filterLayerBtn.onClick = [this] { if (currentLayer != GateLayer::Filter) setLayer(GateLayer::Filter); };
    pitchLayerBtn.setClickingTogglesState(true);
    pitchLayerBtn.setRadioGroupId(0x6a8a);
    pitchLayerBtn.setTooltip("Edit the pitch envelope (shifts osc pitch per-voice)");
    pitchLayerBtn.onClick = [this] { if (currentLayer != GateLayer::Pitch) setLayer(GateLayer::Pitch); };
    addAndMakeVisible(gaterLayerBtn);
    addAndMakeVisible(filterLayerBtn);
    addAndMakeVisible(pitchLayerBtn);

    // ── Bypass (public, VoicePanel binds the APVTS attachment) ───────────────
    bypassButton.setClickingTogglesState(true);
    bypassButton.setTooltip("Bypass the gater so the drone passes through");
    addAndMakeVisible(bypassButton);

    // ── Gap slider (public, VoicePanel binds the APVTS attachment) ───────────
    gapSlider.setRange(0.0, 100.0, 1.0);
    gapSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    gapSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 16);
    gapSlider.setTooltip("Gap: silence at the end of each envelope region (0–100 %)");
    gapSlider.onValueChange = [this] { setGap((float)(gapSlider.getValue() / 100.0)); };
    addAndMakeVisible(gapSlider);

    // ── Subdivision ──────────────────────────────────────────────────────────
    subdivLabel.setText("Grid", juce::dontSendNotification);
    subdivLabel.setJustificationType(juce::Justification::centredRight);
    subdivLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(subdivLabel);
    for (int i = 0; i < kSubdivCount; ++i)
        subdivDropdown.addItem(kSubdivOptions[i].label, i + 1);
    subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    subdivDropdown.onChange = [this](int id) {
        if (id < 1 || id > kSubdivCount) return;
        setSubdivision(kSubdivOptions[id - 1].denom);
    };
    addAndMakeVisible(subdivDropdown);

    // ── Toolbox ──────────────────────────────────────────────────────────────
    arrowBtn  .setTooltip("Arrow — select an envelope to edit its properties");
    pencilBtn .setTooltip("Pencil — draw; drag handles to reshape");
    eraserBtn .setTooltip("Eraser — click to remove");
    glueBtn   .setTooltip("Glue — drag to merge envelopes");
    reverseBtn.setTooltip("Reverse — flip attack/decay");
    for (auto* b : { &arrowBtn, &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->onClick = [this, b] { selectTool(b->tool()); };
        addAndMakeVisible(b);
    }
    pencilBtn.setToggleState(true, juce::dontSendNotification);

    // ── Properties strip: probability rotary ─────────────────────────────────
    probKnob.setRange(1.0, 100.0, 1.0);
    probKnob.setValue(100.0, juce::dontSendNotification);
    probKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    probKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 38, 14);
    probKnob.setTooltip("Probability this envelope plays each loop (1–100 %)");
    probKnob.onValueChange = [this] { if (!propsUpdating) writePropsToSelection(); };
    addAndMakeVisible(probKnob);
    probLabel.setText("Prob", juce::dontSendNotification);
    probLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    probLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(probLabel);

    // ── Properties strip: loop mask ───────────────────────────────────────────
    loopMLabel.setText("Loop", juce::dontSendNotification);
    loopMLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    loopMLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(loopMLabel);

    for (int i = 1; i <= 8; ++i) loopMDropdown.addItem(juce::String(i), i);
    loopMDropdown.setSelectedId(1, false);
    loopMDropdown.onChange = [this](int) {
        if (!propsUpdating) { updateLoopBtnEnabled(); writePropsToSelection(); }
    };
    addAndMakeVisible(loopMDropdown);

    for (int i = 0; i < 8; ++i)
    {
        loopBtn[i].setButtonText(juce::String(i + 1));
        loopBtn[i].setClickingTogglesState(true);
        loopBtn[i].setTooltip("Play on loop position " + juce::String(i + 1));
        loopBtn[i].onClick = [this] { if (!propsUpdating) writePropsToSelection(); };
        addAndMakeVisible(loopBtn[i]);
    }
    loopBtn[0].setToggleState(true, juce::dontSendNotification);  // default: play on every loop

    // All props invisible until an envelope is selected.
    for (juce::Component* c : { static_cast<juce::Component*>(&probKnob),
                                 static_cast<juce::Component*>(&probLabel),
                                 static_cast<juce::Component*>(&loopMLabel),
                                 static_cast<juce::Component*>(&loopMDropdown) })
        c->setVisible(false);
    for (auto& b : loopBtn) b.setVisible(false);
}

void GatingDesigner::selectTool(GateTool t)
{
    if (t != GateTool::Arrow) clearSelection();
    currentTool = t;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void GatingDesigner::setLayer(GateLayer layer)
{
    if (layer == currentLayer) return;
    clearSelection();
    currentLayer = layer;
    gaterLayerBtn .setToggleState(layer == GateLayer::Gater,  juce::dontSendNotification);
    filterLayerBtn.setToggleState(layer == GateLayer::Filter, juce::dontSendNotification);
    pitchLayerBtn .setToggleState(layer == GateLayer::Pitch,  juce::dontSendNotification);
    if (auto* pat = getActivePattern())
    {
        subdivisionDenom = static_cast<int>(pat->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    }
    repaint();
}

void GatingDesigner::setSubdivision(int denominator)
{
    if (denominator == subdivisionDenom && getActivePattern() == nullptr) return;
    subdivisionDenom = denominator;
    subdivDropdown.setSelectedId(idForDenom(denominator), false);
    if (auto* pat = getActivePattern())
        pat->subdivision = static_cast<GatePattern::Subdivision>(denominator);
    repaint();
}

void GatingDesigner::setPattern(GatePattern* pattern)
{
    boundPattern = pattern;
    if (pattern != nullptr && currentLayer == GateLayer::Gater)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    }
    repaint();
}

void GatingDesigner::setFilterPattern(GatePattern* pattern)
{
    filterPattern = pattern;
    if (pattern != nullptr && currentLayer == GateLayer::Filter)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    }
    repaint();
}

void GatingDesigner::setPitchPattern(GatePattern* pattern)
{
    pitchPattern = pattern;
    if (pattern != nullptr && currentLayer == GateLayer::Pitch)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    }
    repaint();
}

void GatingDesigner::setGap(float gap01)
{
    gap01 = juce::jlimit(0.0f, 1.0f, gap01);
    if (std::abs(gap01 - gapValue) < 1e-4f) return;
    gapValue = gap01;
    repaint();
}

juce::Colour GatingDesigner::activeLayerColour() const noexcept
{
    if (currentLayer == GateLayer::Gater)  return MuLookAndFeel::colour(MuLookAndFeel::knobFxSend);
    if (currentLayer == GateLayer::Filter) return MuLookAndFeel::colour(MuLookAndFeel::knobPostPad);
    return MuLookAndFeel::colour(MuLookAndFeel::knobEuclidean);   // Pitch = purple
}
juce::Colour GatingDesigner::ghostLayerColour() const noexcept
{
    // All non-gate layers ghost the gate in coral as a timing reference.
    return MuLookAndFeel::colour(MuLookAndFeel::knobFxSend);
}

int GatingDesigner::cellCount() const noexcept { return kTotalBars * subdivisionDenom; }

juce::Rectangle<float> GatingDesigner::gridBounds() const noexcept
{
    using mu_ui::s;
    return { 0.0f, (float)(s(kHdr1H) + s(kHdr2H)), (float)getWidth(), (float)s(kGridH) };
}

int GatingDesigner::cellAtX(float x) const noexcept
{
    const auto grid = gridBounds();
    const int cells = cellCount();
    if (cells <= 0 || grid.getWidth() <= 0.0f) return -1;
    if (x < grid.getX() || x > grid.getRight()) return -1;
    return juce::jlimit(0, cells - 1, (int)((x - grid.getX()) / grid.getWidth() * (float)cells));
}

GatingDesigner::EnvLayout GatingDesigner::layoutFor(const GateEnvelope& e) const noexcept
{
    using mu_ui::sf;
    EnvLayout L;
    const auto grid = gridBounds();
    const int cells = cellCount();
    if (cells <= 0) return L;
    const float cellW = grid.getWidth() / (float)cells;
    L.x0  = grid.getX() + cellW * (float)e.startCell;
    L.wpx = cellW * (float)e.lengthCells;
    L.top = grid.getY() + sf(2.0f);
    L.bot = grid.getBottom() - sf(2.0f);
    L.h   = L.bot - L.top;
    const float gap      = juce::jlimit(0.0f, 1.0f, gapValue);
    L.activeW            = L.wpx * (1.0f - gap);
    const float split    = juce::jlimit(0.0f, 1.0f, e.split);
    const float peakFrac = e.reverse ? (1.0f - split) : split;
    L.peak = { L.x0 + L.activeW * peakFrac, L.top };
    {
        const float ph = peakFrac * (1.0f - gap) * 0.5f;
        L.riseMid = { L.x0 + L.wpx * ph, L.bot - L.h * e.value(ph, gap) };
    }
    {
        const float ph = (1.0f - gap) * (peakFrac + 1.0f) * 0.5f;
        L.fallMid = { L.x0 + L.wpx * ph, L.bot - L.h * e.value(ph, gap) };
    }
    return L;
}

GatingDesigner::HandleHit GatingDesigner::hitTestHandles(juce::Point<float> p, GatePattern* pat) const noexcept
{
    using mu_ui::sf;
    if (pat == nullptr) return {};
    const float r = sf(kHandleRadius);
    const auto& envs = pat->envelopes;
    for (int i = 0; i < (int)envs.size(); ++i)
    {
        const auto L = layoutFor(envs[(size_t)i]);
        if (p.getDistanceFrom(L.peak)    <= r) return { i, Handle::Split };
        if (p.getDistanceFrom(L.riseMid) <= r) return { i, Handle::RiseBend };
        if (p.getDistanceFrom(L.fallMid) <= r) return { i, Handle::FallBend };
    }
    return {};
}

template <typename Fn>
void GatingDesigner::withLock(GatePattern* pat, Fn&& fn)
{
    if (pat == nullptr) return;
    // Bounded spin — the audio thread holds editLock for at most one block's
    // per-sample loop, but to avoid blocking the message thread for a full audio
    // block (~10 ms at 48 kHz/512), we cap the spin at 1000 yields and give up
    // if we can't acquire. The next drag/click event will retry; the missed edit
    // is imperceptible at typical mouse-event rates (60+ Hz).
    constexpr int kMaxSpins = 1000;
    for (int i = 0; i < kMaxSpins; ++i)
    {
        bool expected = false;
        if (pat->editLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
        {
            fn();
            pat->editLock.store(false, std::memory_order_release);
            return;
        }
        std::this_thread::yield();
    }
    // Contention too long — skip this frame rather than stall the message thread.
}

// ── Selection ─────────────────────────────────────────────────────────────────
void GatingDesigner::clearSelection()
{
    selEnvIndex = -1;
    for (juce::Component* c : { static_cast<juce::Component*>(&probKnob),
                                 static_cast<juce::Component*>(&probLabel),
                                 static_cast<juce::Component*>(&loopMLabel),
                                 static_cast<juce::Component*>(&loopMDropdown) })
        c->setVisible(false);
    for (auto& b : loopBtn) b.setVisible(false);
    repaint();
}

void GatingDesigner::selectEnvelope(int idx)
{
    selEnvIndex = idx;
    const bool valid = (idx >= 0);
    for (juce::Component* c : { static_cast<juce::Component*>(&probKnob),
                                 static_cast<juce::Component*>(&probLabel),
                                 static_cast<juce::Component*>(&loopMLabel),
                                 static_cast<juce::Component*>(&loopMDropdown) })
        c->setVisible(valid);
    for (auto& b : loopBtn) b.setVisible(valid);
    if (valid) updatePropsFromSelection();
    repaint();
}

void GatingDesigner::updateLoopBtnEnabled()
{
    const int m = loopMDropdown.getSelectedId();
    for (int i = 0; i < 8; ++i)
        loopBtn[i].setEnabled(i < m);
}

void GatingDesigner::updatePropsFromSelection()
{
    auto* pat = getActivePattern();
    if (pat == nullptr || selEnvIndex < 0 || selEnvIndex >= (int)pat->envelopes.size())
    { clearSelection(); return; }

    propsUpdating = true;
    const auto& env = pat->envelopes[(size_t)selEnvIndex];
    probKnob.setValue(juce::jlimit(1.0, 100.0, (double)(env.probability * 100.0f)),
                      juce::dontSendNotification);
    loopMDropdown.setSelectedId(juce::jlimit(1, 8, env.loopM), false);
    for (int i = 0; i < 8; ++i)
        loopBtn[i].setToggleState(((env.loopMask >> i) & 1u) != 0, juce::dontSendNotification);
    propsUpdating = false;
    updateLoopBtnEnabled();
}

void GatingDesigner::writePropsToSelection()
{
    auto* pat = getActivePattern();
    if (pat == nullptr || selEnvIndex < 0 || selEnvIndex >= (int)pat->envelopes.size())
        return;
    withLock(pat, [&] {
        auto& env = pat->envelopes[(size_t)selEnvIndex];
        env.probability = juce::jlimit(0.01f, 1.0f, (float)(probKnob.getValue() / 100.0));
        env.loopM       = juce::jlimit(1, 8, loopMDropdown.getSelectedId());
        uint8_t mask = 0;
        for (int i = 0; i < 8; ++i)
            if (loopBtn[i].getToggleState()) mask |= (uint8_t)(1 << i);
        if (mask == 0) mask = 0x01;   // at least one position must play
        env.loopMask = mask;
    });
}

// ── Mouse ─────────────────────────────────────────────────────────────────────
void GatingDesigner::mouseDown(const juce::MouseEvent& e)
{
    auto* pat = getActivePattern();
    if (pat == nullptr) return;
    const auto grid = gridBounds();
    if (!grid.contains(e.position)) return;
    const int cell = cellAtX(e.position.x);
    if (cell < 0) return;

    switch (currentTool)
    {
        case GateTool::Arrow:
        {
            int found = -1;
            const auto& envs = pat->envelopes;
            for (int i = 0; i < (int)envs.size(); ++i)
                if (envs[(size_t)i].covers(cell)) { found = i; break; }
            selectEnvelope(found);
            break;
        }
        case GateTool::Pencil:
        {
            const auto hit = hitTestHandles(e.position, pat);
            if (hit.handle != Handle::None)
            {
                dragEnvIndex = hit.envIndex;
                dragStartY   = e.position.y;
                const auto& env = pat->envelopes[(size_t)hit.envIndex];
                if      (hit.handle == Handle::Split)    dragKind = DragKind::Split;
                else if (hit.handle == Handle::RiseBend) { dragKind = DragKind::RiseBend; dragStartBend = env.reverse ? env.decayBend : env.attackBend; }
                else                                     { dragKind = DragKind::FallBend; dragStartBend = env.reverse ? env.attackBend : env.decayBend; }
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                return;
            }
            withLock(pat, [&] { GateEnvelope env; env.startCell = cell; env.lengthCells = 1; pat->addEnvelope(env); });
            repaint();
            break;
        }
        case GateTool::Eraser:
            withLock(pat, [&] { pat->removeEnvelopeCovering(cell); });
            repaint();
            break;
        case GateTool::Glue:
            dragKind = DragKind::GlueRange;
            glueFirstCell = glueLastCell = cell;
            repaint();
            break;
        case GateTool::Reverse:
            withLock(pat, [&] { for (auto& env : pat->envelopes) if (env.covers(cell)) { env.reverse = !env.reverse; break; } });
            repaint();
            break;
    }
}

void GatingDesigner::mouseDrag(const juce::MouseEvent& e)
{
    auto* pat = getActivePattern();
    if (pat == nullptr) return;
    const int envCount = (int)pat->envelopes.size();

    switch (dragKind)
    {
        case DragKind::Split:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const auto L    = layoutFor(pat->envelopes[(size_t)dragEnvIndex]);
            const float pf  = juce::jlimit(0.0f, 1.0f, (e.position.x - L.x0) / juce::jmax(1.0f, L.activeW));
            withLock(pat, [&] { auto& env = pat->envelopes[(size_t)dragEnvIndex]; env.split = env.reverse ? (1.0f - pf) : pf; });
            repaint();
            break;
        }
        case DragKind::RiseBend:
        case DragKind::FallBend:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const auto L    = layoutFor(pat->envelopes[(size_t)dragEnvIndex]);
            const float nb  = juce::jlimit(-1.0f, 1.0f, dragStartBend + (dragStartY - e.position.y) * 2.0f / juce::jmax(1.0f, L.h));
            withLock(pat, [&] {
                auto& env = pat->envelopes[(size_t)dragEnvIndex];
                if (dragKind == DragKind::RiseBend) (env.reverse ? env.decayBend  : env.attackBend) = nb;
                else                                (env.reverse ? env.attackBend : env.decayBend)  = nb;
            });
            repaint();
            break;
        }
        case DragKind::GlueRange:
        {
            const int c = cellAtX(e.position.x);
            if (c >= 0) { glueLastCell = c; repaint(); }
            break;
        }
        case DragKind::None: break;
    }
}

void GatingDesigner::mouseUp(const juce::MouseEvent& e)
{
    auto* pat = getActivePattern();
    if (pat != nullptr && dragKind == DragKind::GlueRange && glueFirstCell >= 0 && glueLastCell >= 0)
    {
        withLock(pat, [&] { pat->mergeRange(glueFirstCell, glueLastCell); });
        repaint();
    }
    dragKind = DragKind::None;
    dragEnvIndex = -1;
    glueFirstCell = glueLastCell = -1;

    const bool over = pat != nullptr && currentTool == GateTool::Pencil
                   && hitTestHandles(e.position, pat).handle != Handle::None;
    setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GatingDesigner::mouseMove(const juce::MouseEvent& e)
{
    auto* pat = getActivePattern();
    const bool over = pat != nullptr && currentTool == GateTool::Pencil
                   && hitTestHandles(e.position, pat).handle != Handle::None;
    setMouseCursor(over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GatingDesigner::mouseExit(const juce::MouseEvent&)
{
    if (dragKind == DragKind::None) setMouseCursor(juce::MouseCursor::NormalCursor);
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void GatingDesigner::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    using Id = MuLookAndFeel::ColourIds;

    // Header rows background.
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillRect(0, 0, getWidth(), s(kHdr1H) + s(kHdr2H));

    // Thin divider between header rows.
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.4f));
    g.fillRect(0, s(kHdr1H), getWidth(), 1);

    // "Gap" label in row 2.
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.5f))));
    g.drawText("Gap", s(kHdrInset), s(kHdr1H), s(32), s(kHdr2H), juce::Justification::centredLeft, false);

    // Gate grid.
    const auto gateRect = gridBounds();
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBg));
    g.fillRect(gateRect);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(gateRect, 1.0f);

    // Subdivision gridlines.
    const int cells = cellCount();
    if (cells > 0)
    {
        const float cellW = gateRect.getWidth() / (float)cells;
        const int cpb = cells / kTotalBars;
        for (int i = 1; i < cells; ++i)
        {
            const float x = gateRect.getX() + cellW * (float)i;
            const bool bar = (i % cpb) == 0;
            g.setColour(bar ? MuLookAndFeel::colour(Id::headingText).withAlpha(0.55f)
                            : MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
            g.fillRect(x, gateRect.getY() + sf(2.0f), bar ? sf(1.5f) : sf(1.0f), gateRect.getHeight() - sf(4.0f));
        }
    }

    // Glue drag highlight.
    if (dragKind == DragKind::GlueRange && glueFirstCell >= 0 && glueLastCell >= 0 && cells > 0)
    {
        const float cellW = gateRect.getWidth() / (float)cells;
        const int lo = juce::jmin(glueFirstCell, glueLastCell);
        const int hi = juce::jmax(glueFirstCell, glueLastCell);
        g.setColour(activeLayerColour().withAlpha(0.18f));
        g.fillRect(gateRect.getX() + cellW * (float)lo, gateRect.getY(), cellW * (float)(hi - lo + 1), gateRect.getHeight());
    }

    // Draw envelopes.
    auto drawEnvs = [&](GatePattern* pat, juce::Colour col, float alpha, bool showHandles)
    {
        if (pat == nullptr || pat->envelopes.empty()) return;
        const float gap  = (pat == boundPattern) ? juce::jlimit(0.0f, 1.0f, gapValue) : 0.0f;
        const auto  fill = col.withAlpha(alpha * 0.5f);
        const auto  edge = col.withAlpha(alpha);
        const auto& envs = pat->envelopes;
        for (int ei = 0; ei < (int)envs.size(); ++ei)
        {
            const auto& env = envs[(size_t)ei];
            const auto L = layoutFor(env);
            if (L.wpx <= 0.0f) continue;
            juce::Path p;
            p.startNewSubPath(L.x0, L.bot);
            const int steps = juce::jmax(2, (int)L.wpx);
            for (int sx = 0; sx <= steps; ++sx)
            {
                const float ph = (float)sx / (float)steps;
                p.lineTo(L.x0 + L.wpx * ph, L.bot - L.h * env.value(ph, gap));
            }
            p.lineTo(L.x0 + L.wpx, L.bot);
            p.closeSubPath();
            g.setColour(fill); g.fillPath(p);
            g.setColour(edge); g.strokePath(p, juce::PathStrokeType(1.0f));

            // Selection border.
            if (showHandles && currentTool == GateTool::Arrow && ei == selEnvIndex)
            {
                g.setColour(col.brighter(0.4f).withAlpha(alpha));
                g.drawRoundedRectangle(L.x0, L.top, L.wpx, L.h, 2.0f, 1.5f);
            }
        }
        if (showHandles && currentTool == GateTool::Pencil)
        {
            const float r = sf(kHandleRadius);
            for (const auto& env : envs)
            {
                const auto L = layoutFor(env);
                if (L.wpx <= 0.0f) continue;
                g.setColour(col.brighter(0.3f).withAlpha(alpha));
                for (auto pt : { L.riseMid, L.fallMid })
                    g.fillEllipse(pt.x - r * 0.7f, pt.y - r * 0.7f, r * 1.4f, r * 1.4f);
                g.setColour(MuLookAndFeel::colour(Id::textBright).withAlpha(alpha));
                g.fillEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f);
                g.setColour(MuLookAndFeel::colour(Id::panelBackground));
                g.drawEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f, 1.0f);
            }
        }
    };
    drawEnvs(getGhostPattern(),  ghostLayerColour(), 0.18f, false);
    drawEnvs(getActivePattern(), activeLayerColour(), 1.0f, true);

    // Bar number markers.
    if (cells > 0)
    {
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
        for (int bar = 0; bar < kTotalBars; ++bar)
        {
            const float x = gateRect.getX() + (gateRect.getWidth() / kTotalBars) * (float)bar;
            g.drawText(juce::String(bar + 1),
                       juce::Rectangle<float>(x + sf(4.0f), gateRect.getBottom() - sf(14.0f), sf(14.0f), sf(12.0f)),
                       juce::Justification::centredLeft, false);
        }
    }

    // Playhead.
    if (playheadVisible)
    {
        const float x = gateRect.getX() + gateRect.getWidth() * (float)juce::jlimit(0.0, 1.0, playheadBeat01);
        g.setColour(MuLookAndFeel::colour(Id::textBright).withAlpha(0.9f));
        g.fillRect(x - sf(0.5f), gateRect.getY(), sf(1.5f), gateRect.getHeight());
    }

    // Properties strip background.
    const int propsY = s(kHdr1H) + s(kHdr2H) + s(kGridH);
    g.setColour(MuLookAndFeel::colour(Id::panelBackground).darker(0.08f));
    g.fillRect(0, propsY, getWidth(), s(kPropsH));
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.fillRect(0, propsY, getWidth(), 1);

    // Hint text when nothing is selected.
    if (selEnvIndex < 0)
    {
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.5f))));
        g.drawText("Select an envelope with the Arrow tool to edit its properties",
                   s(8), propsY, getWidth() - s(16), s(kPropsH),
                   juce::Justification::centredLeft, false);
    }
    else
    {
        // Section labels.
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.5f))));
        const int divX = getWidth() / 3;
        g.fillRect(divX, propsY + s(6), 1, s(kPropsH) - s(12));

        // "Loop:" header text for the loop-mask section.
        g.setColour(MuLookAndFeel::colour(Id::labelText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
        g.drawText("Loops (of", divX + s(6), propsY + s(4), s(64), s(14),
                   juce::Justification::centredLeft, false);
    }
}

void GatingDesigner::setPlayhead(double beat01, bool visible)
{
    const bool changed = (visible != playheadVisible)
                      || (visible && std::abs(beat01 - playheadBeat01) > 0.0005);
    playheadBeat01 = beat01;
    playheadVisible = visible;
    if (changed) repaint();
}

// ── Resized ───────────────────────────────────────────────────────────────────
void GatingDesigner::resized()
{
    using mu_ui::s;
    using mu_ui::sf;
    const int w = getWidth();
    const int h1H = s(kHdr1H);
    const int h2Y = h1H;
    const int h2H = s(kHdr2H);
    const int propsY  = h1H + h2H + s(kGridH);
    const int propsH  = s(kPropsH);
    const int toolW   = s(kToolW);
    const int toolGap = s(kToolGap);
    const int toolY   = (h1H - toolW) / 2;
    const int ddW     = s(kDdW);
    const int ddH     = h1H - s(2);

    // Header row 1: [GATE][FILT][PITCH] | tools | [Bypass] | [Grid][dropdown]
    const int layerBtnW = s(36);
    gaterLayerBtn .setBounds(s(kHdrInset),                         toolY, layerBtnW, toolW);
    filterLayerBtn.setBounds(s(kHdrInset) + layerBtnW + s(2),     toolY, layerBtnW, toolW);
    pitchLayerBtn .setBounds(s(kHdrInset) + 2 * layerBtnW + s(4), toolY, s(40), toolW);

    int tx = s(kHdrInset) + 2 * layerBtnW + s(4) + s(40) + s(4);
    for (auto* b : { &arrowBtn, &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->setBounds(tx, toolY, toolW, toolW);
        tx += toolW + toolGap;
    }

    subdivDropdown.setBounds(w - ddW - s(kHdrInset),           s(1),  ddW,    ddH);
    subdivLabel   .setBounds(w - ddW - s(kHdrInset) - s(40),   s(1),  s(36),  ddH);

    const int bypassW = s(54);
    bypassButton.setBounds(w - ddW - s(kHdrInset) - s(40) - s(4) - bypassW, toolY, bypassW, toolW);

    // Header row 2: "Gap" (painted) + gap slider
    const int gapLabelW = s(32);
    gapSlider.setBounds(s(kHdrInset) + gapLabelW + s(4), h2Y + (h2H - s(16)) / 2,
                        w - s(kHdrInset) * 2 - gapLabelW - s(4) - s(4), s(16));

    // Properties strip.
    const int knobDiam = s(38);
    const int knobX    = s(kHdrInset) + s(4);
    const int knobY    = propsY + (propsH - knobDiam - s(14)) / 2;
    probKnob .setBounds(knobX, knobY, knobDiam, knobDiam);
    probLabel.setBounds(knobX, knobY + knobDiam + s(1), knobDiam, s(12));

    // Loop section: past first-third divider.
    const int divX   = w / 3;
    const int loopX0 = divX + s(6);

    // "Loop (of M):" occupies a compact row.
    const int loopLbW  = s(50);   // "Loop (of"
    const int mDdW     = s(32);
    int lx = loopX0 + s(4);
    // The "Loop (of" text is painted. Just lay the dropdown + close bracket label.
    loopMLabel.setBounds(lx, propsY + s(4), loopLbW, s(14));   lx += loopLbW;
    loopMDropdown.setBounds(lx, propsY + s(3), mDdW, s(16));    lx += mDdW + s(2);

    // 8 loop position toggle buttons in a 2-row grid of 4.
    const int btnW = s(20), btnH = s(18), btnGap = s(2);
    const int btnsStartX = loopX0;
    const int btnsStartY = propsY + s(22);
    for (int i = 0; i < 8; ++i)
    {
        const int col = i % 4;
        const int row = i / 4;
        loopBtn[i].setBounds(btnsStartX + col * (btnW + btnGap),
                             btnsStartY + row * (btnH + btnGap),
                             btnW, btnH);
    }
}

} // namespace mu_tant
