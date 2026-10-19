#include "RhythmMiniVisual.h"

namespace mu_clid {

RhythmMiniVisual::RhythmMiniVisual(RhythmHost& h, int index)
    : host(h), rhythmIndex(index)
{
    addAndMakeVisible(miniCircle);
    miniCircle.setInterceptsMouseClicks(false, false);

    const auto colour = MuLookAndFeel::layerPalette[
        host.getRhythmLayer(rhythmIndex).colourIndex % MuLookAndFeel::kLayerPaletteSize];

    const Rhythm& r = host.getRhythmLayer(rhythmIndex);
    miniCircle.setPatterns(r.genA.getStepTypes(), r.genB.getStepTypes(), r.genC.getStepTypes());
    miniCircle.setPlayState(&host.getRhythmPlayState(rhythmIndex),
                            &host.getRhythmTransportFlag(), colour);
    miniCircle.setSingleColour(colour);   // the sidebar icon shows the rhythm's own colour

    lastSigA = r.genA.signature();
    lastSigB = r.genB.signature();
    lastSigC = r.genC.signature();
    lastSigValid = true;

    startTimerHz(mu_ui::kUiRefreshHz);
}

void RhythmMiniVisual::resized()
{
    miniCircle.setBounds(getLocalBounds());
}

void RhythmMiniVisual::timerCallback()
{
    // Step-hit edge detection (monotonic counter) → pulse the parent.
    const int currentHitCount = host.getRhythmPlayState(rhythmIndex).hitCount.load();
    if (currentHitCount != lastHitCount)
    {
        lastHitCount = currentHitCount;
        if (onHit) onHit();
    }

    // Re-read the rhythm pattern (base + modulated overrides) only when a cheap
    // POD signature changes — keeps the mini-circle in sync with EuclideanPanel
    // edits + modulation without allocating a vector every tick.
    const Rhythm& r = host.getRhythmLayer(rhythmIndex);
    const auto sigA = r.genA.signature();
    const auto sigB = r.genB.signature();
    const auto sigC = r.genC.signature();
    const EuclidOverrides ov = host.getRhythmEuclidOverrides(rhythmIndex);

    if (! lastSigValid || sigA != lastSigA || sigB != lastSigB || sigC != lastSigC
        || ov != lastAppliedOverrides)
    {
        lastSigA = sigA; lastSigB = sigB; lastSigC = sigC;
        lastAppliedOverrides = ov;
        lastSigValid = true;
        miniCircle.setPatterns(r.genA.getStepTypes(ov.a),
                               r.genB.getStepTypes(ov.b),
                               r.genC.getStepTypes(ov.c));
    }
}

} // namespace mu_clid
