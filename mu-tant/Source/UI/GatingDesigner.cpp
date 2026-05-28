#include "GatingDesigner.h"

#include <cmath>
#include <thread>

namespace mu_tant
{

namespace
{
    // Subdivision dropdown entries — denominator value packed into dropdown ID.
    struct SubdivEntry { int denom; const char* label; };
    constexpr SubdivEntry kSubdivOptions[] = {
        { 4,  "1/4"  },
        { 8,  "1/8"  },
        { 16, "1/16" },
        { 32, "1/32" },
    };
    constexpr int kSubdivCount = (int) (sizeof(kSubdivOptions) / sizeof(kSubdivOptions[0]));

    int idForDenom(int denom)
    {
        for (int i = 0; i < kSubdivCount; ++i)
            if (kSubdivOptions[i].denom == denom) return i + 1;
        return 3; // fallback 1/16
    }
}

// ── Toolbox button — procedural vector icons ──────────────────────────────
GateToolButton::GateToolButton(GateTool t)
    : juce::Button(juce::String()), toolId(t)
{
    setClickingTogglesState(true);
    setRadioGroupId(0x6a7e);   // arbitrary shared id so the 4 tools are exclusive
}

void GateToolButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/)
{
    using Id = MuLookAndFeel::ColourIds;
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced(2.0f);

    // Button background — brighter when selected, subtle on hover.
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
            // Diagonal pencil: shaft line from bottom-left to top-right + a tip.
            juce::Path p;
            p.startNewSubPath(b.getX(), b.getBottom());
            p.lineTo(b.getRight(), b.getY());
            g.strokePath(p, juce::PathStrokeType(1.6f));
            // tip triangle at bottom-left
            juce::Path tip;
            tip.addTriangle(b.getX(), b.getBottom(),
                            b.getX() + b.getWidth() * 0.28f, b.getBottom() - b.getHeight() * 0.12f,
                            b.getX() + b.getWidth() * 0.12f, b.getBottom() - b.getHeight() * 0.28f);
            g.fillPath(tip);
            break;
        }
        case GateTool::Eraser:
        {
            // Rubber block — a parallelogram.
            juce::Path p;
            p.startNewSubPath(b.getX() + b.getWidth() * 0.2f, b.getBottom());
            p.lineTo(b.getRight(),                          b.getBottom());
            p.lineTo(b.getRight() - b.getWidth() * 0.2f,    b.getY());
            p.lineTo(b.getX(),                              b.getY());
            p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.4f));
            break;
        }
        case GateTool::Glue:
        {
            // Glue droplet — a teardrop (circle + point on top).
            const float cx = b.getCentreX();
            const float cy = b.getCentreY() + b.getHeight() * 0.12f;
            const float rad = b.getWidth() * 0.32f;
            g.fillEllipse(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
            juce::Path drop;
            drop.addTriangle(cx, b.getY(),
                             cx - rad * 0.7f, cy,
                             cx + rad * 0.7f, cy);
            g.fillPath(drop);
            break;
        }
        case GateTool::Reverse:
        {
            // Two opposing horizontal arrows.
            const float midY = b.getCentreY();
            const float q    = b.getHeight() * 0.22f;
            juce::Path p;
            // top arrow pointing right
            p.startNewSubPath(b.getX(), midY - q);
            p.lineTo(b.getRight(), midY - q);
            // bottom arrow pointing left
            p.startNewSubPath(b.getRight(), midY + q);
            p.lineTo(b.getX(), midY + q);
            g.strokePath(p, juce::PathStrokeType(1.4f));
            // arrowheads
            juce::Path heads;
            heads.addTriangle(b.getRight(), midY - q,
                              b.getRight() - q, midY - q - q * 0.6f,
                              b.getRight() - q, midY - q + q * 0.6f);
            heads.addTriangle(b.getX(), midY + q,
                              b.getX() + q, midY + q - q * 0.6f,
                              b.getX() + q, midY + q + q * 0.6f);
            g.fillPath(heads);
            break;
        }
    }
}

GatingDesigner::GatingDesigner()
{
    subdivLabel.setText("Grid", juce::dontSendNotification);
    subdivLabel.setJustificationType(juce::Justification::centredRight);
    subdivLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    addAndMakeVisible(subdivLabel);

    for (int i = 0; i < kSubdivCount; ++i)
        subdivDropdown.addItem(kSubdivOptions[i].label, i + 1);
    subdivDropdown.setSelectedId(idForDenom(subdivisionDenom), false);
    subdivDropdown.onChange = [this](int id)
    {
        if (id < 1 || id > kSubdivCount) return;
        setSubdivision(kSubdivOptions[id - 1].denom);
    };
    addAndMakeVisible(subdivDropdown);

    // ── Toolbox (pencil / eraser / glue / reverse) ──────────────────────────
    pencilBtn .setTooltip("Pencil — draw an envelope; drag its handles to reshape");
    eraserBtn .setTooltip("Eraser — click an envelope to remove it");
    glueBtn   .setTooltip("Glue — drag across envelopes to merge them");
    reverseBtn.setTooltip("Reverse — click an envelope to flip attack/decay");
    for (auto* b : { &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->onClick = [this, b] { selectTool(b->tool()); };
        addAndMakeVisible(b);
    }
    pencilBtn.setToggleState(true, juce::dontSendNotification);
}

void GatingDesigner::selectTool(GateTool t)
{
    currentTool = t;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();   // show/hide the pencil grab-handles
}

void GatingDesigner::setSubdivision(int denominator)
{
    if (denominator == subdivisionDenom && boundPattern == nullptr) return;
    subdivisionDenom = denominator;
    subdivDropdown.setSelectedId(idForDenom(denominator), false);
    if (boundPattern != nullptr)
        boundPattern->subdivision = static_cast<GatePattern::Subdivision>(denominator);
    repaint();
}

void GatingDesigner::setPattern(GatePattern* pattern)
{
    boundPattern = pattern;
    if (pattern != nullptr)
    {
        // Pull the bound pattern's subdivision so the UI shows the persisted
        // value when the user switches voices.
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

int GatingDesigner::cellCount() const noexcept
{
    // 2 bars: cellCount = 2 * denom (one cell per Nth-note over the bar span).
    return kTotalBars * subdivisionDenom;
}

juce::Rectangle<float> GatingDesigner::gridBounds() const noexcept
{
    using mu_ui::s;
    return { 0.0f, (float) s(kHeaderH), (float) getWidth(), (float) s(kGridH) };
}

int GatingDesigner::cellAtX(float x) const noexcept
{
    const auto grid = gridBounds();
    const int cells = cellCount();
    if (cells <= 0 || grid.getWidth() <= 0.0f) return -1;
    if (x < grid.getX() || x > grid.getRight()) return -1;
    const float rel = (x - grid.getX()) / grid.getWidth();
    return juce::jlimit(0, cells - 1, (int) (rel * (float) cells));
}

GatingDesigner::EnvLayout GatingDesigner::layoutFor(const GateEnvelope& e) const noexcept
{
    using mu_ui::sf;
    EnvLayout L;
    const auto grid = gridBounds();
    const int cells = cellCount();
    if (cells <= 0) return L;

    const float cellW = grid.getWidth() / (float) cells;
    L.x0  = grid.getX() + cellW * (float) e.startCell;
    L.wpx = cellW * (float) e.lengthCells;
    L.top = grid.getY() + sf(2.0f);
    L.bot = grid.getBottom() - sf(2.0f);
    L.h   = L.bot - L.top;

    const float gap = juce::jlimit(0.0f, 1.0f, gapValue);
    L.activeW = L.wpx * (1.0f - gap);

    const float split    = juce::jlimit(0.0f, 1.0f, e.split);
    const float peakFrac = e.reverse ? (1.0f - split) : split;   // p at the visual peak

    L.peak = { L.x0 + L.activeW * peakFrac, L.top };

    // Rising-segment midpoint (region start → peak), evaluated on the curve.
    {
        const float ph = peakFrac * (1.0f - gap) * 0.5f;         // full-region phase
        L.riseMid = { L.x0 + L.wpx * ph, L.bot - L.h * e.value(ph, gap) };
    }
    // Falling-segment midpoint (peak → active end), evaluated on the curve.
    {
        const float ph = (1.0f - gap) * (peakFrac + 1.0f) * 0.5f;
        L.fallMid = { L.x0 + L.wpx * ph, L.bot - L.h * e.value(ph, gap) };
    }
    return L;
}

GatingDesigner::HandleHit GatingDesigner::hitTestHandles(juce::Point<float> p) const noexcept
{
    using mu_ui::sf;
    if (boundPattern == nullptr) return {};
    const float r = sf(kHandleRadius);
    const auto& envs = boundPattern->envelopes;
    for (int i = 0; i < (int) envs.size(); ++i)
    {
        const auto L = layoutFor(envs[(size_t) i]);
        if (p.getDistanceFrom(L.peak)    <= r) return { i, Handle::Split };
        if (p.getDistanceFrom(L.riseMid) <= r) return { i, Handle::RiseBend };
        if (p.getDistanceFrom(L.fallMid) <= r) return { i, Handle::FallBend };
    }
    return {};
}

template <typename Fn>
void GatingDesigner::withPatternLock(Fn&& fn)
{
    if (boundPattern == nullptr) return;
    while (boundPattern->editLock.exchange(true, std::memory_order_acquire))
        std::this_thread::yield();
    fn();
    boundPattern->editLock.store(false, std::memory_order_release);
}

void GatingDesigner::setPlayhead(double beat01, bool visible)
{
    // Skip redundant repaints — only invalidate when the line actually moves
    // a visible amount or visibility flips.
    const bool changed = (visible != playheadVisible)
                       || (visible && std::abs(beat01 - playheadBeat01) > 0.0005);
    playheadBeat01  = beat01;
    playheadVisible = visible;
    if (changed) repaint();
}

// ── Mouse interaction ──────────────────────────────────────────────────────
void GatingDesigner::mouseDown(const juce::MouseEvent& e)
{
    if (boundPattern == nullptr) return;
    const auto grid = gridBounds();
    if (!grid.contains(e.position)) return;
    const int cell = cellAtX(e.position.x);
    if (cell < 0) return;

    switch (currentTool)
    {
        case GateTool::Pencil:
        {
            // Reshape an existing envelope if a grab-handle was hit.
            const auto hit = hitTestHandles(e.position);
            if (hit.handle != Handle::None)
            {
                dragEnvIndex = hit.envIndex;
                dragStartY   = e.position.y;
                const auto& env = boundPattern->envelopes[(size_t) hit.envIndex];
                if (hit.handle == Handle::Split)
                {
                    dragKind = DragKind::Split;
                }
                else if (hit.handle == Handle::RiseBend)
                {
                    dragKind      = DragKind::RiseBend;
                    dragStartBend = env.reverse ? env.decayBend : env.attackBend;
                }
                else // FallBend
                {
                    dragKind      = DragKind::FallBend;
                    dragStartBend = env.reverse ? env.attackBend : env.decayBend;
                }
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                return;
            }
            // Otherwise draw a fresh 1-cell envelope at the clicked cell.
            withPatternLock([&]
            {
                GateEnvelope env;
                env.startCell   = cell;
                env.lengthCells = 1;
                boundPattern->addEnvelope(env);
            });
            repaint();
            break;
        }
        case GateTool::Eraser:
            withPatternLock([&] { boundPattern->removeEnvelopeCovering(cell); });
            repaint();
            break;
        case GateTool::Glue:
            dragKind      = DragKind::GlueRange;
            glueFirstCell = cell;
            glueLastCell  = cell;
            repaint();
            break;
        case GateTool::Reverse:
            withPatternLock([&]
            {
                for (auto& env : boundPattern->envelopes)
                    if (env.covers(cell)) { env.reverse = !env.reverse; break; }
            });
            repaint();
            break;
    }
}

void GatingDesigner::mouseDrag(const juce::MouseEvent& e)
{
    if (boundPattern == nullptr) return;
    const int envCount = (int) boundPattern->envelopes.size();

    switch (dragKind)
    {
        case DragKind::Split:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const auto L = layoutFor(boundPattern->envelopes[(size_t) dragEnvIndex]);
            const float denom    = juce::jmax(1.0f, L.activeW);
            const float peakFrac = juce::jlimit(0.0f, 1.0f, (e.position.x - L.x0) / denom);
            withPatternLock([&]
            {
                auto& env = boundPattern->envelopes[(size_t) dragEnvIndex];
                env.split = env.reverse ? (1.0f - peakFrac) : peakFrac;
            });
            repaint();
            break;
        }
        case DragKind::RiseBend:
        case DragKind::FallBend:
        {
            if (dragEnvIndex < 0 || dragEnvIndex >= envCount) return;
            const auto L = layoutFor(boundPattern->envelopes[(size_t) dragEnvIndex]);
            const float sens    = 2.0f / juce::jmax(1.0f, L.h);   // full-height drag ≈ full ±range
            const float newBend = juce::jlimit(-1.0f, 1.0f,
                                               dragStartBend + (dragStartY - e.position.y) * sens);
            withPatternLock([&]
            {
                auto& env = boundPattern->envelopes[(size_t) dragEnvIndex];
                // Rising handle = attack (or decay when reversed); falling = the other.
                if (dragKind == DragKind::RiseBend)
                    (env.reverse ? env.decayBend  : env.attackBend) = newBend;
                else
                    (env.reverse ? env.attackBend : env.decayBend)  = newBend;
            });
            repaint();
            break;
        }
        case DragKind::GlueRange:
        {
            const int cell = cellAtX(e.position.x);
            if (cell >= 0) { glueLastCell = cell; repaint(); }
            break;
        }
        case DragKind::None:
            break;
    }
}

void GatingDesigner::mouseUp(const juce::MouseEvent& e)
{
    if (boundPattern != nullptr && dragKind == DragKind::GlueRange
        && glueFirstCell >= 0 && glueLastCell >= 0)
    {
        withPatternLock([&] { boundPattern->mergeRange(glueFirstCell, glueLastCell); });
        repaint();
    }

    dragKind      = DragKind::None;
    dragEnvIndex  = -1;
    glueFirstCell = -1;
    glueLastCell  = -1;

    // Restore the hover cursor for the current tool / position.
    const bool overHandle = currentTool == GateTool::Pencil
                          && hitTestHandles(e.position).handle != Handle::None;
    setMouseCursor(overHandle ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::NormalCursor);
}

void GatingDesigner::mouseMove(const juce::MouseEvent& e)
{
    const bool overHandle = boundPattern != nullptr
                         && currentTool == GateTool::Pencil
                         && hitTestHandles(e.position).handle != Handle::None;
    setMouseCursor(overHandle ? juce::MouseCursor::PointingHandCursor
                              : juce::MouseCursor::NormalCursor);
}

void GatingDesigner::mouseExit(const juce::MouseEvent&)
{
    if (dragKind == DragKind::None)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void GatingDesigner::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    using Id = MuLookAndFeel::ColourIds;

    // Header strip with title text on the left.
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillRect(0, 0, getWidth(), s(kHeaderH));
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    g.drawText("Gating", s(kHeaderInset), 0, s(120), s(kHeaderH),
               juce::Justification::centredLeft, false);

    // Gate rectangle — full width below the header.
    const auto gateRect = gridBounds();
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBg));
    g.fillRect(gateRect);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.drawRect(gateRect, 1.0f);

    // Subdivision gridlines. Two bars total — bar boundary is the bolder line.
    const int cells = cellCount();
    if (cells <= 0) return;
    const float cellW = gateRect.getWidth() / (float) cells;
    const int cellsPerBar = cells / kTotalBars;

    for (int i = 1; i < cells; ++i)
    {
        const float x = gateRect.getX() + cellW * (float) i;
        const bool isBarLine = (i % cellsPerBar) == 0;
        g.setColour(isBarLine
                    ? MuLookAndFeel::colour(Id::headingText).withAlpha(0.55f)
                    : MuLookAndFeel::colour(Id::mutedText).withAlpha(0.25f));
        g.fillRect(x, gateRect.getY() + sf(2.0f), isBarLine ? sf(1.5f) : sf(1.0f),
                   gateRect.getHeight() - sf(4.0f));
    }

    // ── Glue drag range highlight ──────────────────────────────────────────
    if (dragKind == DragKind::GlueRange && glueFirstCell >= 0 && glueLastCell >= 0)
    {
        const int lo = juce::jmin(glueFirstCell, glueLastCell);
        const int hi = juce::jmax(glueFirstCell, glueLastCell);
        const float rx = gateRect.getX() + cellW * (float) lo;
        const float rw = cellW * (float) (hi - lo + 1);
        g.setColour(MuLookAndFeel::colour(Id::knobFxSend).withAlpha(0.18f));
        g.fillRect(rx, gateRect.getY(), rw, gateRect.getHeight());
    }

    // ── Envelopes ───────────────────────────────────────────────────────────
    // Each envelope's filled height at any x is the 0..1 gate the engine outputs
    // (attack/decay shape + the trailing Gap silence), so the editor matches the
    // audio exactly.
    if (boundPattern != nullptr && !boundPattern->envelopes.empty())
    {
        const float gap  = juce::jlimit(0.0f, 1.0f, gapValue);
        const auto  fill = MuLookAndFeel::colour(Id::knobFxSend).withAlpha(0.5f);
        const auto  edge = MuLookAndFeel::colour(Id::knobFxSend);

        const auto& envs = boundPattern->envelopes;
        for (const auto& env : envs)
        {
            const auto L = layoutFor(env);
            if (L.wpx <= 0.0f) continue;

            juce::Path p;
            p.startNewSubPath(L.x0, L.bot);
            const int steps = juce::jmax(2, (int) L.wpx);
            for (int sx = 0; sx <= steps; ++sx)
            {
                const float ph  = (float) sx / (float) steps;   // 0..1 across region
                const float val = env.value(ph, gap);
                p.lineTo(L.x0 + L.wpx * ph, L.bot - L.h * val);
            }
            p.lineTo(L.x0 + L.wpx, L.bot);
            p.closeSubPath();
            g.setColour(fill);
            g.fillPath(p);
            g.setColour(edge);
            g.strokePath(p, juce::PathStrokeType(1.0f));
        }

        // Grab handles — only with the Pencil tool, drawn on top of the curves.
        if (currentTool == GateTool::Pencil)
        {
            const float r = sf(kHandleRadius);
            for (const auto& env : envs)
            {
                const auto L = layoutFor(env);
                if (L.wpx <= 0.0f) continue;

                // Bend handles (rising / falling line midpoints).
                g.setColour(MuLookAndFeel::colour(Id::knobFxSend).brighter(0.3f));
                for (auto pt : { L.riseMid, L.fallMid })
                    g.fillEllipse(pt.x - r * 0.7f, pt.y - r * 0.7f, r * 1.4f, r * 1.4f);

                // Peak / split handle (on top).
                g.setColour(MuLookAndFeel::colour(Id::textBright));
                g.fillEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f);
                g.setColour(MuLookAndFeel::colour(Id::panelBackground));
                g.drawEllipse(L.peak.x - r, L.peak.y - r, r * 2.0f, r * 2.0f, 1.0f);
            }
        }
    }

    // "1 / 2" bar markers along the bottom edge for orientation.
    g.setColour(MuLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(9.0f))));
    for (int bar = 0; bar < kTotalBars; ++bar)
    {
        const float x = gateRect.getX() + (gateRect.getWidth() / kTotalBars) * (float) bar;
        g.drawText(juce::String(bar + 1),
                   juce::Rectangle<float>(x + sf(4.0f),
                                          gateRect.getBottom() - sf(14.0f),
                                          sf(14.0f), sf(12.0f)),
                   juce::Justification::centredLeft, false);
    }

    // ── Playback timeline ─────────────────────────────────────────────────────
    if (playheadVisible)
    {
        const float x = gateRect.getX()
                      + gateRect.getWidth() * (float) juce::jlimit(0.0, 1.0, playheadBeat01);
        g.setColour(MuLookAndFeel::colour(Id::textBright).withAlpha(0.9f));
        g.fillRect(x - sf(0.5f), gateRect.getY(), sf(1.5f), gateRect.getHeight());
    }
}

void GatingDesigner::resized()
{
    using mu_ui::s;
    const int w = getWidth();
    const int hdrH = s(kHeaderH);
    const int ddW  = s(kDropdownW);
    const int ddH  = hdrH - s(2);
    // Header right side: [Grid] [dropdown]
    subdivDropdown.setBounds(w - ddW - s(kHeaderInset), s(1), ddW, ddH);
    subdivLabel   .setBounds(w - ddW - s(kHeaderInset) - s(40), s(1), s(36), ddH);

    // Toolbox row — sits after the "Gating" title (which paint() draws at ~x=6
    // spanning ~120 px), left of the Grid label.
    const int toolW = s(kToolW);
    const int toolGap = s(kToolGap);
    const int toolY = (hdrH - toolW) / 2;
    int tx = s(kHeaderInset) + s(70);   // past the "Gating" title text
    for (auto* b : { &pencilBtn, &eraserBtn, &glueBtn, &reverseBtn })
    {
        b->setBounds(tx, toolY, toolW, toolW);
        tx += toolW + toolGap;
    }
}

} // namespace mu_tant
