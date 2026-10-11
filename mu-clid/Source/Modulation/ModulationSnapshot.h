#pragma once

#include "UI/EuclideanPanel.h"   // mu_ui::EuclidSnap, pinned to the kSnapEuc* run below

namespace mu_clid {

// Per-destination modulated-value snapshot indices.
// Written by the audio thread (PluginProcessor::processBlock) after
// ModulationMatrix::process(); read by UI panels at ~30 Hz to drive the
// live-arc modulation indicator.  Values are pre-normalised 0..1 to each
// knob's display range.
//
// Lives here (not on PluginProcessor) so VoiceSection / EuclideanPanel can
// consume the indices without pulling in the full processor header.
// Access the values via PluginProcessor::getModSnapshot(rhythmIndex, snapIndex).
enum ModSnapIdx : int
{
    kSnapAmpAtk = 0, kSnapAmpDec, kSnapAmpSus, kSnapAmpRel,
    kSnapFilterCutoff, kSnapFilterRes,
    kSnapFenvAtk, kSnapFenvDec, kSnapFenvDepth,
    kSnapPitchSemi,
    // Stage 36: 4 generic insert Param slots (normalised 0..1) replace the
    // prior 5 semantically-named slots. Each algorithm interprets the slot
    // through the per-algo config table in Source/Audio/InsertSlotConfig.h.
    kSnapInsP1, kSnapInsP2, kSnapInsP3, kSnapInsP4,
    kSnapPitchEnvDep, kSnapAmpLvl, kSnapAccent,
    kSnapEucAHits,  kSnapEucARotate, kSnapEucAPrePad, kSnapEucAPostPad, kSnapEucAInsSt, kSnapEucAInsLen,
    kSnapEucBHits,  kSnapEucBRotate, kSnapEucBPrePad, kSnapEucBPostPad, kSnapEucBInsSt, kSnapEucBInsLen,
    kSnapEucCHits,  kSnapEucCRotate, kSnapEucCPrePad, kSnapEucCPostPad, kSnapEucCInsSt, kSnapEucCInsLen,
    // T5 follow-up — new mod destinations introduced after audit.
    kSnapFilterLowCut,
    kSnapPitchOctave,
    // Mixer strip: pan (-1..+1) and the three FX sends (0..1), actual values.
    kSnapPan, kSnapSendEff, kSnapSendDly, kSnapSendRev,
    kSnapCount
};

// The Euclid panel reports its knobs as an offset into the kSnapEuc* run: the 18 entries must stay
// contiguous and in the panel's order.
static_assert(kSnapEucAHits + mu_ui::EuclidSnap::kEucAInsLen == kSnapEucAInsLen);
static_assert(kSnapEucAHits + mu_ui::EuclidSnap::kEucBHits   == kSnapEucBHits);
static_assert(kSnapEucAHits + mu_ui::EuclidSnap::kEucCHits   == kSnapEucCHits);
static_assert(kSnapEucAHits + mu_ui::EuclidSnap::kEucCInsLen == kSnapEucCInsLen);
static_assert(mu_ui::EuclidSnap::kEucSnapCount == 18);

} // namespace mu_clid
