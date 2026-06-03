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

    // ── Gap rotary (public, VoicePanel binds the APVTS attachment) ──────────
    gapSlider.setRange(0.0, 100.0, 1.0);
    gapSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    gapSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, kGapTbW, 16);
    gapSlider.setTooltip("Gap: silence at the end of each envelope region (0-100 %)");
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
    pencilBtn .setTooltip("Pencil - draw; drag handles to reshape");
    eraserBtn .setTooltip("Eraser - click to remove");
    reverseBtn.setTooltip("Reverse - flip attack/decay");
    for (auto* b : { &pencilBtn, &eraserBtn, &reverseBtn })
    {
        b->onClick = [this, b] { selectTool(b->tool()); };
        addAndMakeVisible(b);
    }
    pencilBtn.setToggleState(true, juce::dontSendNotification);

    // ── Pattern length dropdown (1-16 bars) ──────────────────────────────────
    for (int b = 1; b <= GatePattern::kMaxPatternBars; ++b)
        barsDropdown.addItem(juce::String(b), b);
    barsDropdown.setSelectedId(2, false);
    barsDropdown.onChange = [this](int id) { if (id >= 1) setPatternBars(id); };
    addAndMakeVisible(barsDropdown);

    // ── Scrollbar ────────────────────────────────────────────────────────────
    scrollBar.setRangeLimits(0.0, 2.0);
    scrollBar.setCurrentRange(0.0, 2.0);
    scrollBar.setAutoHide(false);
    scrollBar.addListener(this);
    addAndMakeVisible(scrollBar);
}

void GatingDesigner::selectTool(GateTool t)
{
    currentTool = t;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    markPathsDirty();
    repaint();
}

void GatingDesigner::setLayer(GateLayer layer)
{
    if (layer == currentLayer) return;
    currentLayer = layer;
    gaterLayerBtn .setToggleState(layer == GateLayer::Gater,  juce::dontSendNotification);
    filterLayerBtn.setToggleState(layer == GateLayer::Filter, juce::dontSendNotification);
    pitchLayerBtn .setToggleState(layer == GateLayer::Pitch,  juce::dontSendNotification);
    if (auto* pat = getActivePattern())
    {
        subdivisionDenom = static_cast<int>(pat->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    }
    markPathsDirty();
    repaint();
}

void GatingDesigner::setSubdivision(int denominator)
{
    if (denominator == subdivisionDenom && getActivePattern() == nullptr) return;
    subdivisionDenom = denominator;
    subdivDropdown.setSelectedId(idForDenom(denominator), false);
    if (auto* pat = getActivePattern())
        withLock(pat, [&] { pat->subdivision = static_cast<GatePattern::Subdivision>(denominator); });
    markPathsDirty();
    repaint();
}

void GatingDesigner::setPatternBars(int bars)
{
    bars = juce::jlimit(1, GatePattern::kMaxPatternBars, bars);
    barsDropdown.setSelectedId(bars, false);

    // Update all three patterns (they share the same temporal grid).
    for (auto* pat : { boundPattern, filterPattern, pitchPattern })
    {
        if (pat == nullptr) continue;
        withLock(pat, [&] {
            pat->patternLengthBars = bars;
            // Remove envelopes that start at or beyond the new total cells.
            const int maxCell = bars * (int) pat->subdivision;
            pat->envelopes.erase(
                std::remove_if(pat->envelopes.begin(), pat->envelopes.end(),
                    [maxCell](const GateEnvelope& e) { return e.startCell >= maxCell; }),
                pat->envelopes.end());
            // Clip any envelope extending past the new end.
            for (auto& e : pat->envelopes)
            {
                const int end = e.startCell + e.lengthCells;
                if (end > maxCell) e.lengthCells = maxCell - e.startCell;
            }
            pat->hasEnvelopes.store(!pat->envelopes.empty(), std::memory_order_relaxed);
        });
    }

    setViewStart(0.0);   // snap view back to start on length change
    updateScrollRange();
    markPathsDirty();
    repaint();
}

void GatingDesigner::updateScrollRange()
{
    auto* pat = getActivePattern();
    const int bars = (pat != nullptr) ? pat->patternLengthBars : kViewBars;
    const double total = (double) bars;
    const double view  = (double) kViewBars;
    scrollBar.setRangeLimits(0.0, total);
    scrollBar.setCurrentRange(juce::jlimit(0.0, total - view, viewStartBar), view);
    // Only show scrollbar when pattern is longer than the view window.
    scrollBar.setVisible(bars > kViewBars);
}

void GatingDesigner::setViewStart(double bar)
{
    auto* pat = getActivePattern();
    const int bars = (pat != nullptr) ? pat->patternLengthBars : kViewBars;
    const double maxStart = (double) juce::jmax(0, bars - kViewBars);
    viewStartBar = juce::jlimit(0.0, maxStart, bar);
    scrollBar.setCurrentRange(viewStartBar, (double) kViewBars, juce::dontSendNotification);
    markPathsDirty();
    repaint();
}

void GatingDesigner::setPattern(GatePattern* pattern)
{
    boundPattern = pattern;
    markPathsDirty();
    if (pattern != nullptr && currentLayer == GateLayer::Gater)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
        barsDropdown.setSelectedId(pattern->patternLengthBars, false);
    }
    setViewStart(0.0);
    updateScrollRange();
    repaint();
}

void GatingDesigner::setFilterPattern(GatePattern* pattern)
{
    filterPattern = pattern;
    markPathsDirty();
    if (pattern != nullptr && currentLayer == GateLayer::Filter)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
        barsDropdown.setSelectedId(pattern->patternLengthBars, false);
    }
    setViewStart(0.0);
    updateScrollRange();
    repaint();
}

void GatingDesigner::setPitchPattern(GatePattern* pattern)
{
    pitchPattern = pattern;
    markPathsDirty();
    if (pattern != nullptr && currentLayer == GateLayer::Pitch)
    {
        subdivisionDenom = static_cast<int>(pattern->subdivision);
        subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
        barsDropdown.setSelectedId(pattern->patternLengthBars, false);
    }
    setViewStart(0.0);
    updateScrollRange();
    repaint();
}

void GatingDesigner::setGap(float gap01)
{
    gap01 = juce::jlimit(0.0f, 1.0f, gap01);
    if (std::abs(gap01 - gapValue) < 1e-4f) return;
    gapValue = gap01;
    markPathsDirty();
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

int GatingDesigner::cellCount() const noexcept
{
    if (auto* p = getActivePattern()) return p->patternLengthBars * subdivisionDenom;
    return kViewBars * subdivisionDenom;
}

juce::Rectangle<float> GatingDesigner::gridBounds() const noexcept
{
    using mu_ui::s;
    return { 0.0f, (float)s(kHdr1H), (float)getWidth(), (float)s(kGridH) };
}

// Pixel width of one cell in the view (based on viewCellCount = kViewBars * subdivisionDenom).
float GatingDesigner::viewCellW() const noexcept
{
    const float viewCells = (float)(kViewBars * subdivisionDenom);
    return viewCells > 0.0f ? gridBounds().getWidth() / viewCells : 0.0f;
}

int GatingDesigner::cellAtX(float x) const noexcept
{
    const auto grid = gridBounds();
    const int totalCells = cellCount();
    if (totalCells <= 0 || grid.getWidth() <= 0.0f) return -1;
    if (x < grid.getX() || x > grid.getRight()) return -1;
    const float cw = viewCellW();
    if (cw <= 0.0f) return -1;
    const float viewStartBeat = (float)(viewStartBar * subdivisionDenom);
    const int cell = (int)(viewStartBeat + (x - grid.getX()) / cw);
    return juce::jlimit(0, totalCells - 1, cell);
}

GatingDesigner::EnvLayout GatingDesigner::layoutFor(const GateEnvelope& e) const noexcept
{
    using mu_ui::sf;
    EnvLayout L;
    const auto grid = gridBounds();
    const float cellW = viewCellW();
    if (cellW <= 0.0f) return L;
    const float viewStartBeat = (float)(viewStartBar * subdivisionDenom);
    L.x0  = grid.getX() + cellW * ((float)e.startCell - viewStartBeat);
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
    // Start/end grab handles tested first so they take priority over body drags.
    for (int i = 0; i < (int)envs.size(); ++i)
    {
        const auto L = layoutFor(envs[(size_t)i]);
        const auto startPt = juce::Point<float>(L.x0,           L.bot);
        const auto endPt   = juce::Point<float>(L.x0 + L.wpx,  L.bot);
        if (p.getDistanceFrom(startPt) <= r) return { i, Handle::StartGrab };
        if (p.getDistanceFrom(endPt)   <= r) return { i, Handle::EndGrab };
    }
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

// ── Path cache ────────────────────────────────────────────────────────────────
void GatingDesigner::rebuildPathCache()
{
    auto buildPaths = [&](GatePattern* pat, std::vector<juce::Path>& cache)
    {
        cache.clear();
        if (pat == nullptr) return;
        // Pre-size the cache before acquiring editLock so Path object construction
        // (which may allocate) doesn't extend the lock hold. The audio thread's
        // bounded spin can exhaust if the lock is held during allocation.
        // We read envelopes.size() without the lock here — safe because only the
        // message thread writes envelopes, so a concurrent read is coherent.
        cache.resize(pat->envelopes.size());
        withLock(pat, [&]
        {
            const float gap = (pat == boundPattern) ? juce::jlimit(0.0f, 1.0f, gapValue) : 0.0f;
            // Adjust if envelopes changed between the pre-size and the lock acquire.
            cache.resize(pat->envelopes.size());
            for (std::size_t ei = 0; ei < pat->envelopes.size(); ++ei)
            {
                const auto& env = pat->envelopes[ei];
                const auto L = layoutFor(env);
                juce::Path& p = cache[ei];
                p.clear();
                if (L.wpx <= 0.0f) continue;
                p.startNewSubPath(L.x0, L.bot);
                const int steps = juce::jmax(2, (int)L.wpx);
                for (int sx = 0; sx <= steps; ++sx)
                {
                    const float ph = (float)sx / (float)steps;
                    p.lineTo(L.x0 + L.wpx * ph, L.bot - L.h * env.value(ph, gap));
                }
                p.lineTo(L.x0 + L.wpx, L.bot);
                p.closeSubPath();
            }
        });
    };
    buildPaths(getActivePattern(), activePathCache);
    buildPaths(getGhostPattern(),  ghostPathCache);
    pathsDirty = false;
}

// ── Selection ─────────────────────────────────────────────────────────────────

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
        case GateTool::Pencil:
        {
            const auto hit = hitTestHandles(e.position, pat);
            if (hit.handle == Handle::StartGrab || hit.handle == Handle::EndGrab)
            {
                dragEnvIndex    = hit.envIndex;
                const auto& env = pat->envelopes[(size_t)hit.envIndex];
                dragOriginalEnd = env.startCell + env.lengthCells - 1;
                dragKind = (hit.handle == Handle::StartGrab) ? DragKind::StartEdge : DragKind::EndEdge;
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
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
            markPathsDirty();
            repaint();
            break;
        }
        case GateTool::Eraser:
            withLock(pat, [&] { pat->removeEnvelopeCovering(cell); });
            markPathsDirty();
            repaint();
            break;
        case GateTool::Reverse:
            withLock(pat, [&] { for (auto& env : pat->envelopes) if (env.covers(cell)) { env.reverse = !env.reverse; break; } });
            markPathsDirty();
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
            const auto L  = layoutFor(pat->envelopes[(size_t)dragEnvIndex]);
            float pf = juce::jlimit(0.0f, 1.0f, (e.position.x - L.x0) / juce::jmax(1.0f, L.activeW));
            if (!e.mods.isAltDown())
            {
                // Snap peak to cell boundaries within the envelope (same as corner handles).
                // ALT held = free fine control.
                const int cells = pat->envelopes[(size_t)dragEnvIndex].lengthCells;
                if (cells > 0)
                    pf = std::round(pf * (float)cells) / (float)cells;
            }
            withLock(pat, [&] { auto& env = pat->envelopes[(size_t)dragEnvIndex]; env.split = env.reverse ? (1.0f - pf) : pf; });
            markPathsDirty();
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
            markPathsDirty();
            repaint();
            break;
        }
        case DragKind::StartEdge:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const int newStart = juce::jlimit(0, dragOriginalEnd, cellAtX(e.position.x));
            withLock(pat, [&] {
                auto& env = pat->envelopes[(size_t)dragEnvIndex];
                env.startCell   = newStart;
                env.lengthCells = juce::jmax(1, dragOriginalEnd - newStart + 1);
            });
            markPathsDirty();
            repaint();
            break;
        }
        case DragKind::EndEdge:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const int curStart = pat->envelopes[(size_t)dragEnvIndex].startCell;
            const int newEnd   = juce::jmax(curStart, cellAtX(e.position.x));
            withLock(pat, [&] {
                pat->envelopes[(size_t)dragEnvIndex].lengthCells = newEnd - curStart + 1;
            });
            markPathsDirty();
            repaint();
            break;
        }
        case DragKind::None: break;
    }
}

void GatingDesigner::mouseUp(const juce::MouseEvent& e)
{
    const bool wasDragging = (dragKind != DragKind::None);
    dragKind        = DragKind::None;
    dragEnvIndex    = -1;
    dragOriginalEnd = -1;
    if (wasDragging) markPathsDirty();

    auto* pat = getActivePattern();
    const bool overHandle = pat != nullptr
                         && hitTestHandles(e.position, pat).handle != Handle::None;
    const bool isEdge = overHandle && (hitTestHandles(e.position, pat).handle == Handle::StartGrab
                                    || hitTestHandles(e.position, pat).handle == Handle::EndGrab);
    if (isEdge)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (overHandle && currentTool == GateTool::Pencil)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void GatingDesigner::mouseMove(const juce::MouseEvent& e)
{
    auto* pat = getActivePattern();
    if (pat == nullptr) { setMouseCursor(juce::MouseCursor::NormalCursor); return; }
    const auto hit = hitTestHandles(e.position, pat);
    if (hit.handle == Handle::StartGrab || hit.handle == Handle::EndGrab)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (hit.handle != Handle::None && currentTool == GateTool::Pencil)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
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

    // Header row background.
    const int hdrH = s(kHdr1H);
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillRect(0, 0, getWidth(), hdrH);

    // "Gap" label painted just before the gap rotary. Position mirrors resized():
    // Bypass(kBtnW) + layers(3 × kBtnW) + tools(3 × kToolW) + gaps.
    const int afterLeftBtns = s(kHdrInset) + 4 * s(kBtnW) + 3 * s(4) + s(6);
    const int afterTools    = afterLeftBtns + 3 * (s(kToolW) + s(kToolGap)) + s(6);
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.5f))));
    g.drawText("Gap", afterTools, 0, s(26), hdrH, juce::Justification::centredLeft, false);

    // "Bars" label painted immediately left of the bars dropdown.
    g.drawText("Bars", barsDropdown.getX() - s(26), 0, s(26), hdrH,
               juce::Justification::centredLeft, false);

    // Gate grid.
    const auto gateRect = gridBounds();
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBg));
    g.fillRect(gateRect);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(gateRect, 1.0f);

    // Subdivision gridlines — rendered in view-coordinate space.
    const float cw = viewCellW();
    const float viewStartBeat = (float)(viewStartBar * subdivisionDenom);
    if (cw > 0.0f)
    {
        const int totalCells = cellCount();
        const int viewCells  = kViewBars * subdivisionDenom;
        for (int i = 0; i <= viewCells; ++i)
        {
            const int absCell = (int)(viewStartBeat) + i;
            if (absCell < 0 || absCell > totalCells) continue;
            const float x = gateRect.getX() + cw * (float)i;
            const bool isBar = (absCell % subdivisionDenom) == 0;
            g.setColour(isBar ? MuLookAndFeel::colour(Id::headingText).withAlpha(0.55f)
                              : MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
            g.fillRect(x, gateRect.getY() + sf(2.0f), isBar ? sf(1.5f) : sf(1.0f), gateRect.getHeight() - sf(4.0f));
        }
    }

    // Draw envelopes from cached paths; rebuild the cache if stale.
    if (pathsDirty) rebuildPathCache();

    auto drawEnvPaths = [&](GatePattern* pat, const std::vector<juce::Path>& paths,
                             juce::Colour col, float alpha, bool showHandles)
    {
        if (pat == nullptr || paths.empty()) return;
        const auto fill = col.withAlpha(alpha * 0.5f);
        const auto edge = col.withAlpha(alpha);
        const auto& envs = pat->envelopes;
        const int  n    = juce::jmin((int)paths.size(), (int)envs.size());
        for (int ei = 0; ei < n; ++ei)
        {
            const auto& p = paths[(size_t)ei];
            if (p.isEmpty()) continue;
            g.setColour(fill); g.fillPath(p);
            g.setColour(edge); g.strokePath(p, juce::PathStrokeType(1.0f));
        }
        if (showHandles)
        {
            const float r = sf(kHandleRadius);
            for (int ei = 0; ei < n; ++ei)
            {
                const auto L = layoutFor(envs[(size_t)ei]);
                if (L.wpx <= 0.0f) continue;

                // Start/end grab handles (bottom-left / bottom-right corners).
                const auto startPt = juce::Point<float>(L.x0,          L.bot);
                const auto endPt   = juce::Point<float>(L.x0 + L.wpx, L.bot);
                g.setColour(col.withAlpha(alpha));
                g.fillRect(juce::Rectangle<float>(startPt.x - r * 0.6f, startPt.y - r * 0.6f, r * 1.2f, r * 1.2f));
                g.fillRect(juce::Rectangle<float>(endPt  .x - r * 0.6f, endPt  .y - r * 0.6f, r * 1.2f, r * 1.2f));

                // Pencil shape handles (peak + bend midpoints).
                if (currentTool == GateTool::Pencil)
                {
                    g.setColour(col.brighter(0.3f).withAlpha(alpha));
                    for (auto pt : { L.riseMid, L.fallMid })
                        g.fillEllipse(pt.x - r * 0.7f, pt.y - r * 0.7f, r * 1.4f, r * 1.4f);
                    g.setColour(MuLookAndFeel::colour(Id::textBright).withAlpha(alpha));
                    g.fillEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f);
                    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
                    g.drawEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f, 1.0f);
                }
            }
        }
    };
    drawEnvPaths(getGhostPattern(),  ghostPathCache,  ghostLayerColour(), 0.18f, false);
    drawEnvPaths(getActivePattern(), activePathCache, activeLayerColour(), 1.0f, true);

    // Absolute bar number markers at each visible bar boundary.
    if (cw > 0.0f)
    {
        g.setColour(MuLookAndFeel::colour(Id::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
        const int firstBar = (int)viewStartBar;
        const int lastBar  = firstBar + kViewBars + 1;
        for (int absBar = firstBar; absBar <= lastBar; ++absBar)
        {
            const float cellOffset = (float)(absBar * subdivisionDenom) - viewStartBeat;
            const float x = gateRect.getX() + cw * cellOffset;
            if (x < gateRect.getX() - 1.0f || x > gateRect.getRight()) continue;
            g.drawText(juce::String(absBar + 1),
                       juce::Rectangle<float>(x + sf(4.0f), gateRect.getBottom() - sf(14.0f), sf(20.0f), sf(12.0f)),
                       juce::Justification::centredLeft, false);
        }
    }

    // Playhead — convert pattern-relative beat01 to view-relative pixel position.
    // beat01 is fraction of the full pattern length; the view shows kViewBars at offset viewStartBar.
    if (playheadVisible && cw > 0.0f)
    {
        const auto* activePat = getActivePattern();
        const int patBars = (activePat != nullptr) ? activePat->patternLengthBars : kViewBars;
        const float absoluteBar = (float)(playheadBeat01 * patBars);
        const float viewRelFrac = (absoluteBar - (float)viewStartBar) / (float)kViewBars;
        if (viewRelFrac >= 0.0f && viewRelFrac <= 1.0f)
        {
        const float x = gateRect.getX() + gateRect.getWidth() * viewRelFrac;
        g.setColour(MuLookAndFeel::colour(Id::textBright).withAlpha(0.9f));
        g.fillRect(x - sf(0.5f), gateRect.getY(), sf(1.5f), gateRect.getHeight());
        } // viewRelFrac in range
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
    markPathsDirty();
    using mu_ui::s;
    const int w      = getWidth();
    const int hdrH   = s(kHdr1H);
    const int gridY  = hdrH;
    const int gridH  = s(kGridH);
    const int scrollY = gridY + gridH;
    const int toolW   = s(kToolW);
    const int toolGap = s(kToolGap);
    const int toolY   = (hdrH - toolW) / 2;
    const int ddW     = s(kDdW);
    const int ddH     = hdrH - s(2);

    // ── Scrollbar ─────────────────────────────────────────────────────────────
    scrollBar.setBounds(0, scrollY, w, s(kScrollH));

    // ── Right-anchored controls ────────────────────────────────────────────────
    // "Grid" label + subdivision dropdown at far right.
    subdivDropdown.setBounds(w - ddW - s(kHdrInset),         s(1), ddW,   ddH);
    subdivLabel   .setBounds(w - ddW - s(kHdrInset) - s(40), s(1), s(36), ddH);

    // "Bars" label + dropdown immediately left of the Grid controls.
    const int barsLabelW = s(26);
    const int barsGroupRight = w - ddW - s(kHdrInset) - s(40) - s(6);
    const int barsDropX = barsGroupRight - s(kBarsW);
    const int barsLabelX = barsDropX - barsLabelW;
    barsDropdown.setBounds(barsDropX, s(1), s(kBarsW), ddH);

    // ── Left-anchored controls (left→right) ────────────────────────────────────
    // [Bypass][GATE][FILT][PITCH] — all same width kBtnW | [✏][⌫][⟺] | Gap ●
    const int btnW = s(kBtnW);
    int lx = s(kHdrInset);
    bypassButton .setBounds(lx, toolY, btnW, toolW); lx += btnW + s(4);
    gaterLayerBtn .setBounds(lx, toolY, btnW, toolW); lx += btnW + s(4);
    filterLayerBtn.setBounds(lx, toolY, btnW, toolW); lx += btnW + s(4);
    pitchLayerBtn .setBounds(lx, toolY, btnW, toolW); lx += btnW + s(6);

    // Tools (Pencil, Eraser, Reverse — no Arrow).
    for (auto* b : { &pencilBtn, &eraserBtn, &reverseBtn })
    {
        b->setBounds(lx, toolY, toolW, toolW);
        lx += toolW + toolGap;
    }
    lx += s(6);

    // Gap: "Gap" label painted, then rotary.
    lx += s(26);   // painted "Gap" label width
    gapSlider.setBounds(lx, (hdrH - s(kGapKnobW)) / 2, s(kGapKnobW) + s(kGapTbW), s(kGapKnobW));
    lx += s(kGapKnobW) + s(kGapTbW) + s(10);

    // Selection props: "Prob" painted label + prob rotary (hidden until selection).
    lx += s(28);   // painted "Prob" label width
}

} // namespace mu_tant
