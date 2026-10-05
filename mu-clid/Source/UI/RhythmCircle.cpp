#include "RhythmCircle.h"

namespace
{
    // Animation timing — all derived from a single timer Hz so the durations
    // stay in real-time units if the timer rate is ever changed.
    constexpr float kTimerHz       = 30.0f;
    constexpr float kSnapEaseSec   = 0.15f;   // ease-out to 0 when transport stops
    constexpr float kPulseLifeSec  = 0.15f;   // arc-pulse display lifetime per hit
    constexpr float kHubDecaySec   = 0.30f;   // centre-hub flash fade

    constexpr float kSnapStep      = 1.0f / (kSnapEaseSec  * kTimerHz);
    constexpr float kPulseSteps    = kPulseLifeSec * kTimerHz;
    constexpr float kHubAlphaStart = 0.8f;    // hub flash starting alpha
    constexpr float kHubDecayStep  = kHubAlphaStart / (kHubDecaySec * kTimerHz);
}

RhythmCircle::RhythmCircle()
{
    arcPulses.fill({});
    startTimerHz((int) kTimerHz);
}

RhythmCircle::~RhythmCircle()
{
    stopTimer();
}

void RhythmCircle::setPatterns(const std::vector<StepType>& patA,
                                const std::vector<StepType>& patB,
                                const std::vector<StepType>& patC)
{
    patternA = patA;
    patternB = patB;
    patternC = patC;
    repaint();
}

void RhythmCircle::setPlayState(PluginProcessor::RhythmPlayState*  state,
                                 const std::atomic<float>*          beatFrac,
                                 const std::atomic<bool>*            playing,
                                 juce::Colour                         colour)
{
    playState     = state;
    beatFracAtom  = beatFrac;
    isPlayingAtom = playing;
    rhythmColour  = colour;
}

//==============================================================================
void RhythmCircle::triggerHitPulse(int combinedStep, int stepsA)
{
    if (stepsA <= 0) return;
    const int ringAStep = combinedStep % stepsA;
    auto& p    = arcPulses[nextPulse % kMaxPulses];
    p.stepFrac = (float)ringAStep / (float)stepsA;
    p.arcWidth = juce::MathConstants<float>::twoPi / (float)stepsA;
    // Full alpha on the playhead pulse so it dominates against the static rings.
    p.alpha    = 1.0f;
    p.expand   = 0.0f;
    p.active   = true;
    ++nextPulse;
    hubAlpha = kHubAlphaStart;
}

void RhythmCircle::timerCallback()
{
    bool dirty = false;

    // ── Read play state ──────────────────────────────────────────────────────
    if (playState && beatFracAtom && isPlayingAtom)
    {
        const bool playing = isPlayingAtom->load();

        if (playing)
        {
            const int   step  = playState->currentStep.load();
            const int   stepC = playState->currentStepC.load();
            const float frac  = beatFracAtom->load();
            const int   sA    = juce::jmax(1, playState->stepsA.load());
            const int   sB    = juce::jmax(1, playState->stepsB.load());
            const int   sC    = juce::jmax(1, playState->stepsC.load());
            const float twoPi = juce::MathConstants<float>::twoPi;

            // Each ring rotates at its own speed: one full turn per sX steps.
            // Ring C uses its own independent step counter so its rotation is not
            // disrupted by the combined-pattern (A+B) wrap boundary.
            rotAngleA = ((float)(step % sA) + frac) / (float)sA * twoPi;
            rotAngleB = ((float)(step % sB) + frac) / (float)sB * twoPi;
            rotAngleC = ((float)stepC        + frac) / (float)sC * twoPi;

            // edge-detect via monotonic counter so multiple readers
            // (this circle + sidebar mini-circles + sidebar pulse) can all observe
            // the same hit without racing each other on a shared one-shot flag.
            const int currentHitCount = playState->hitCount.load();
            if (currentHitCount != lastHitCount)
            {
                lastHitCount = currentHitCount;
                triggerHitPulse(step, sA);
            }

            snapFromA    = rotAngleA;
            snapFromB    = rotAngleB;
            snapFromC    = rotAngleC;
            snapProgress = 1.0f;
            wasPlaying   = true;
            dirty        = true;
        }
        else if (wasPlaying)
        {
            wasPlaying   = false;
            snapProgress = 0.0f;
        }
    }

    // ── Ease-out snap to 0 when stopped ─────────────────────────────────────
    if (!wasPlaying && snapProgress < 1.0f)
    {
        snapProgress = juce::jmin(1.0f, snapProgress + kSnapStep);
        const float ease = snapProgress * (2.0f - snapProgress);
        rotAngleA = snapFromA * (1.0f - ease);
        rotAngleB = snapFromB * (1.0f - ease);
        rotAngleC = snapFromC * (1.0f - ease);
        dirty     = true;
    }

    // ── Advance arc pulses ───────────────────────────────────────────────────
    for (auto& p : arcPulses)
    {
        if (!p.active) continue;
        p.expand = juce::jmin(1.0f, p.expand + (1.0f / kPulseSteps));
        const float t = p.expand;
        p.alpha = 1.0f - t * (2.0f - t);  // ease-out-quad fade
        if (p.alpha <= 0.0f) p.active = false;
        dirty = true;
    }

    // ── Hub pulse decay ──────────────────────────────────────────────────────
    if (hubAlpha > 0.0f)
    {
        hubAlpha = juce::jmax(0.0f, hubAlpha - kHubDecayStep);
        dirty = true;
    }

    if (dirty) repaint();
}

//==============================================================================
juce::Colour RhythmCircle::stepColour(StepType t, juce::Colour hitClr, bool isCurrent)
{
    using Id = MuLookAndFeel::ColourIds;
    juce::Colour base;
    switch (t)
    {
        case StepType::Hit:       base = hitClr; break;
        case StepType::PrePad:    base = MuLookAndFeel::colour(Id::ringPrePad)   .withAlpha(0.5f); break;
        case StepType::PostPad:   base = MuLookAndFeel::colour(Id::ringPostPad)  .withAlpha(0.5f); break;
        case StepType::InsertPad: base = MuLookAndFeel::colour(Id::ringInsertPad).withAlpha(0.5f); break;
        default:                  base = hitClr.withAlpha(0.18f); break;
    }
    if (isCurrent) base = base.brighter(0.5f);
    return base;
}

void RhythmCircle::RingCache::rebuild(float cx_, float cy_, float outerR_, float innerR_, int N)
{
    stepPaths.clear();
    stepPaths.reserve((size_t) N);

    const float twoPi   = juce::MathConstants<float>::twoPi;
    const float stepAng = twoPi / (float) N;
    const float gapAng  = juce::jmin(0.05f, stepAng * 0.15f);
    const float arcAng  = stepAng - gapAng;

    for (int i = 0; i < N; ++i)
    {
        // Build geometry with NO rotation (step 0 anchored at 12 o'clock in JUCE arc
        // convention). Per-paint rotation is applied via AffineTransform at fillPath.
        const float a0 = (float) i * stepAng;
        const float a1 = a0 + arcAng;
        juce::Path seg;
        seg.addCentredArc(cx_, cy_, outerR_, outerR_, 0.0f, a0, a1, true);
        seg.addCentredArc(cx_, cy_, innerR_, innerR_, 0.0f, a1, a0, false);
        seg.closeSubPath();
        stepPaths.push_back(std::move(seg));
    }

    cx = cx_; cy = cy_; outerR = outerR_; innerR = innerR_; stepCount = N;
}

void RhythmCircle::drawRing(juce::Graphics& g,
                              const std::vector<StepType>& pattern,
                              float cx, float cy,
                              float outerR, float innerR,
                              juce::Colour hitClr,
                              int currentStep,
                              float rotOff,
                              RingCache& cache) const
{
    const int N = (int)pattern.size();
    if (N == 0 || outerR <= innerR || innerR < 0.0f) return;

    if (!cache.matches(cx, cy, outerR, innerR, N))
        cache.rebuild(cx, cy, outerR, innerR, N);

    // JUCE's addCentredArc measures angles from 12 o'clock (top) and
    // increases clockwise (radians: 0 = top, π/2 = right, π = bottom, -π/2 =
    // left). Cached paths anchor step 0 at 12 o'clock; rotate by `rotOff`
    // (positive = clockwise on screen) so the playhead view matches the prior
    // `startOff = -rotOff` math without rebuilding geometry every frame.
    const auto transform = juce::AffineTransform::rotation(-rotOff, cx, cy);
    using LF = MuLookAndFeel;

    // Recessed track: the ring's whole annulus in shadow, so the gaps between steps read
    // as grooves cut into the panel.
    juce::Path annulus;
    annulus.addCentredArc(cx, cy, outerR, outerR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    annulus.addCentredArc(cx, cy, innerR, innerR, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
    annulus.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(LF::kRingTrackAlpha));
    g.fillPath(annulus);

    // Steps: hits get a soft halo first (stronger on the playhead), then every step's fill.
    const float glowW = (outerR - innerR) * LF::kRingHitGlowWidth;
    for (int i = 0; i < N; ++i)
    {
        const bool isCur = (i == currentStep);
        if (pattern[i] == StepType::Hit)
        {
            g.setColour(hitClr.withAlpha(LF::kRingHitGlowAlpha * (isCur ? 1.8f : 1.0f)));
            g.strokePath(cache.stepPaths[(size_t) i], juce::PathStrokeType(glowW), transform);
        }
        g.setColour(stepColour(pattern[i], hitClr, isCur));
        g.fillPath(cache.stepPaths[(size_t) i], transform);
    }

    // Bevel: darker at the ring's inner edge, a touch lighter at its outer edge, so the
    // band reads as curved rather than flat.
    {
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(annulus);
        const double inner = innerR / outerR;
        juce::ColourGradient bevel(juce::Colours::black.withAlpha(LF::kRingBevelDarkAlpha), cx, cy,
                                   juce::Colours::white.withAlpha(LF::kRingBevelLightAlpha), cx + outerR, cy, true);
        bevel.addColour(inner, juce::Colours::black.withAlpha(LF::kRingBevelDarkAlpha));
        bevel.addColour(inner + (1.0 - inner) * 0.55, juce::Colours::transparentBlack);
        g.setGradientFill(bevel);
        g.fillPath(annulus);
    }

    const float startOff = -rotOff;  // retained for the loop-point divider below

    // Loop-point divider: radial line at the boundary between step N-1 and step 0.
    // use the same addCentredArc convention as the segments above —
    // x = cx + r·sin(angle), y = cy − r·cos(angle) — instead of cos/sin
    // standard-math (which left the divider at 12 o'clock while segments
    // started at 9). Now divider and segments share the same anchor.
    using Id = MuLookAndFeel::ColourIds;
    const float loopSin = std::sin(startOff);
    const float loopCos = std::cos(startOff);
    g.setColour(MuLookAndFeel::colour(Id::panelBackground).brighter(0.6f).withAlpha(0.85f));
    g.drawLine(cx + innerR * loopSin, cy - innerR * loopCos,
               cx + outerR * loopSin, cy - outerR * loopCos, 1.5f);
}

void RhythmCircle::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    const float cx   = getWidth()  * 0.5f;
    const float cy   = getHeight() * 0.5f;
    const float maxR = juce::jmin(cx, cy) - 3.0f;

    if (maxR < 8.0f) return;

    const float ringW   = juce::jmax(4.0f, maxR * 0.16f);
    const float ringGap = juce::jmax(2.0f, maxR * 0.05f);

    // Per-ring current step is derived from pattern size below to avoid a race
    // between the pattern update and the stepsA/stepsB atomics.
    const int combinedStep = playState ? playState->currentStep.load() : 0;

    // ── Ring A — purple ──────────────────────────────────────────────────────
    const float aOuter = maxR;
    const float aInner = aOuter - ringW;
    // Use pattern size for modulo — avoids a race between pattern update and stepsA atomic.
    const int stepA = patternA.empty() ? 0 : combinedStep % (int)patternA.size();
    if (!patternA.empty())
    {
        drawRing(g, patternA, cx, cy, aOuter, aInner,
                 MuLookAndFeel::colour(Id::ringEuclidA), stepA, rotAngleA,
                 ringCaches[0]);
    }
    else
    {
        g.setColour(MuLookAndFeel::colour(Id::ringEuclidA).withAlpha(0.12f));
        juce::Path ring;
        ring.addCentredArc(cx, cy, aOuter, aOuter, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        ring.addCentredArc(cx, cy, aInner, aInner, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
        ring.closeSubPath();
        g.fillPath(ring);
    }

    // ── Ring B — coral ───────────────────────────────────────────────────────
    const float bOuter = aInner - ringGap;
    const float bInner = bOuter - ringW;
    float innerLimit   = (patternB.empty() ? aInner : bInner) - ringGap;

    const int stepB = patternB.empty() ? 0 : combinedStep % (int)patternB.size();
    if (bInner > 0.0f)
    {
        if (!patternB.empty())
        {
            drawRing(g, patternB, cx, cy, bOuter, bInner,
                     MuLookAndFeel::colour(Id::ringEuclidB), stepB, rotAngleB,
                     ringCaches[1]);
        }
        else
        {
            g.setColour(MuLookAndFeel::colour(Id::ringEuclidB).withAlpha(0.12f));
            juce::Path ring;
            ring.addCentredArc(cx, cy, bOuter, bOuter, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            ring.addCentredArc(cx, cy, bInner, bInner, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
            ring.closeSubPath();
            g.fillPath(ring);
        }
    }

    // ── Ring C — amber dashed ────────────────────────────────────────────────
    if (!patternC.empty())
    {
        const float cOuter = bInner - ringGap;
        const float cInner = cOuter - ringW;
        if (cInner > 0.0f)
        {
            drawRing(g, patternC, cx, cy, cOuter, cInner,
                     MuLookAndFeel::colour(Id::ringEuclidC), -1, rotAngleC,
                     ringCaches[2]);
            innerLimit = cInner - ringGap;
        }
    }

    // ── Expanding arc pulses (expand outward from Ring A, rotate with Ring A) ─
    for (const auto& p : arcPulses)
    {
        if (!p.active || p.alpha <= 0.0f) continue;

        const float expandPx   = p.expand * 14.0f;
        const float pulseOuter = aOuter + expandPx;

        const float twoPi  = juce::MathConstants<float>::twoPi;
        // drop the −halfPi offset so the pulse aligns with 12 o'clock —
        // mirrors the same fix in drawRing's startOff.
        const float startA = -rotAngleA + p.stepFrac * twoPi - p.arcWidth * 0.5f;
        const float endA   = startA + p.arcWidth;

        g.setColour(rhythmColour.withAlpha(p.alpha));
        juce::Path pulse;
        pulse.addCentredArc(cx, cy, pulseOuter, pulseOuter, 0.0f, startA, endA, true);
        pulse.addCentredArc(cx, cy, aOuter,     aOuter,     0.0f, endA, startA, false);
        pulse.closeSubPath();
        g.fillPath(pulse);
    }

    using LF = MuLookAndFeel;

    // ── Light over the whole ring area: top-right highlight, bottom-left shade ──
    {
        juce::Path rings;
        rings.addEllipse(cx - aOuter, cy - aOuter, aOuter * 2.0f, aOuter * 2.0f);
        const juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(rings);
        const float reach = aOuter * 1.25f;
        juce::ColourGradient light(juce::Colours::white.withAlpha(LF::kRingLightAlpha),
                                   cx + aOuter * 0.6f, cy - aOuter * 0.6f,
                                   juce::Colours::transparentWhite, cx + aOuter * 0.6f + reach, cy - aOuter * 0.6f, true);
        g.setGradientFill(light);
        g.fillRect(getLocalBounds());
        juce::ColourGradient shade(juce::Colours::black.withAlpha(LF::kRingShadowAlpha),
                                   cx - aOuter * 0.65f, cy + aOuter * 0.65f,
                                   juce::Colours::transparentBlack, cx - aOuter * 0.65f + reach, cy + aOuter * 0.65f, true);
        g.setGradientFill(shade);
        g.fillRect(getLocalBounds());
    }

    // ── Centre hub: a raised, embossed disc lit from the top right, flash on top ──
    if (innerLimit > 4.0f)
    {
        const float hr = innerLimit * LF::kRingHubInset;
        const juce::Rectangle<float> hub(cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);
        juce::Path hubPath;
        hubPath.addEllipse(hub);

        // Cast shadow, falling down-left.
        juce::DropShadow(juce::Colours::black.withAlpha(LF::kRingHubShadowAlpha),
                         (int) juce::jmax(3.0f, hr * 0.22f),
                         { -(int) juce::jmax(1.0f, hr * 0.07f), (int) juce::jmax(1.0f, hr * 0.09f) })
            .drawForPath(g, hubPath);

        // Face: lit crown at the top right shading to the lower left.
        const auto base = MuLookAndFeel::colour(Id::panelBackground);
        juce::ColourGradient face(base.brighter(0.35f), cx + hr * 0.45f, cy - hr * 0.45f,
                                  base.darker(0.45f),   cx - hr * 0.75f, cy + hr * 0.85f, true);
        face.addColour(0.45, base.brighter(0.08f));
        g.setGradientFill(face);
        g.fillPath(hubPath);

        // Specular highlight, clipped to the disc.
        {
            const juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(hubPath);
            const float lx = cx + hr * 0.45f, ly = cy - hr * 0.45f, lr = hr * 0.9f;
            juce::ColourGradient spec(juce::Colours::white.withAlpha(LF::kRingHubSpecAlpha), lx, ly,
                                      juce::Colours::transparentWhite, lx + lr, ly, true);
            g.setGradientFill(spec);
            g.fillEllipse(lx - lr, ly - lr, lr * 2.0f, lr * 2.0f);
        }

        // Rim: catches the light top-right, falls into shade bottom-left.
        juce::ColourGradient rim(juce::Colours::white.withAlpha(LF::kRingHubRimAlpha), cx + hr * 0.7f, cy - hr * 0.7f,
                                 juce::Colours::black.withAlpha(LF::kRingHubRimAlpha * 1.4f), cx - hr * 0.7f, cy + hr * 0.7f, false);
        g.setGradientFill(rim);
        g.strokePath(hubPath, juce::PathStrokeType(juce::jmax(1.0f, hr * 0.025f)));

        if (hubAlpha > 0.0f)
        {
            juce::ColourGradient flash(rhythmColour.withAlpha(hubAlpha * LF::kRingHubFlashAlpha), cx, cy,
                                       rhythmColour.withAlpha(0.0f), cx + hr, cy, true);
            g.setGradientFill(flash);
            g.fillPath(hubPath);
        }
    }
}
