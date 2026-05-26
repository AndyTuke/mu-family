#pragma once

// Per-destination modulated-value snapshot indices.
// Written by the audio thread (PluginProcessor::processBlock) after
// ModulationMatrix::process(); read by UI panels at ~30 Hz to drive the
// live-arc modulation indicator.  Values are pre-normalised 0..1 to each
// knob's display range.
//
// Lives here (not on PluginProcessor) so VoiceSection / EuclideanPanel can
// consume the indices without pulling in the full processor header.
// Access the values via PluginProcessor::getModSnapshot(rhythmIdx, snapIdx).
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
    kSnapCount
};
