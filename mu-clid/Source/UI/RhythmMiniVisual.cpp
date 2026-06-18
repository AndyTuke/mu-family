#include "RhythmMiniVisual.h"

RhythmMiniVisual::RhythmMiniVisual(PluginProcessor& p, int index)
    : proc(p), rhythmIndex(index)
{
    addAndMakeVisible(miniCircle);
    miniCircle.setInterceptsMouseClicks(false, false);

    const auto colour = MuLookAndFeel::channelPalette[
        proc.getRhythm(rhythmIndex).colourIndex % MuLookAndFeel::kChannelPaletteSize];

    const Rhythm& r = proc.getRhythm(rhythmIndex);
    miniCircle.setPatterns(r.genA.getStepTypes(), r.genB.getStepTypes(), r.genC.getStepTypes());
    miniCircle.setPlayState(&proc.rhythmPlayState[rhythmIndex], &proc.beatFraction,
                            &proc.sequencerPlaying, colour);

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
    const int currentHitCount = proc.rhythmPlayState[rhythmIndex].hitCount.load();
    if (currentHitCount != lastHitCount)
    {
        lastHitCount = currentHitCount;
        if (onHit) onHit();
    }

    // Re-read the rhythm pattern (base + modulated overrides) only when a cheap
    // POD signature changes — keeps the mini-circle in sync with EuclideanPanel
    // edits + modulation without allocating a vector every tick.
    const Rhythm& r = proc.getRhythm(rhythmIndex);
    const auto sigA = r.genA.signature();
    const auto sigB = r.genB.signature();
    const auto sigC = r.genC.signature();
    const EuclidOverrides ov = proc.getModulatedEuclidOverrides(rhythmIndex);

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
