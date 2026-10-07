#pragma once

#include "FilterFamilies.h"

// type 6 — 24 dB/oct bandpass via JUCE's LadderFilter (Moog-style).
class Bp24Filter : public LadderFilter24<juce::dsp::LadderFilterMode::BPF24> {};
