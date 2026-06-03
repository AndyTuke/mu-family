#pragma once

// Per-destination modulated-value snapshot indices for mu-tant.
// Written by the audio thread (PluginProcessor::renderVoice) after
// ModulationMatrix::process(); read by VoicePanel at ~30 Hz via
// getTantSnap() to drive the live-arc modulation indicator on each knob.
// Values are actual display units (Hz, dB, semitones, 0..1 normalised
// fractions), matching the order in MuTantModDest::kModDestTable.
namespace mu_tant
{
    enum ModSnapIdx : int
    {
        kTantSnapOsc1Octave = 0,
        kTantSnapOsc1Semi,
        kTantSnapOsc1Fine,
        kTantSnapOsc1Pos,
        kTantSnapOsc2Octave,
        kTantSnapOsc2Semi,
        kTantSnapOsc2Fine,
        kTantSnapOsc2Pos,
        kTantSnapXModFm,
        kTantSnapXModAm,
        kTantSnapXModRing,
        kTantSnapOsc1Level,
        kTantSnapOsc2Level,
        kTantSnapNoiseLevel,
        kTantSnapFilterCutoff,
        kTantSnapFilterRes,
        kTantSnapLevel,
        kTantSnapInsP1,
        kTantSnapInsP2,
        kTantSnapInsP3,
        kTantSnapInsP4,
        kTantSnapCount
    };
} // namespace mu_tant
