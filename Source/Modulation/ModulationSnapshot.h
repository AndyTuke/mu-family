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
    kSnapInsDrive, kSnapInsOutput, kSnapInsBits, kSnapInsDither, kSnapInsLpf,
    kSnapPitchEnvDep, kSnapAmpLvl, kSnapAccent,
    kSnapEucAHits,  kSnapEucARotate, kSnapEucAPrePad, kSnapEucAPostPad, kSnapEucAInsSt, kSnapEucAInsLen,
    kSnapEucBHits,  kSnapEucBRotate, kSnapEucBPrePad, kSnapEucBPostPad, kSnapEucBInsSt, kSnapEucBInsLen,
    kSnapEucCHits,  kSnapEucCRotate, kSnapEucCPrePad, kSnapEucCPostPad, kSnapEucCInsSt, kSnapEucCInsLen,
    kSnapCount
};
