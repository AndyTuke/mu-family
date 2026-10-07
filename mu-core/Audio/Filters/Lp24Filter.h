#pragma once

#include "FilterFamilies.h"

// type 4 — 24 dB/oct lowpass via JUCE's LadderFilter (Moog-style).
class Lp24Filter : public LadderFilter24<juce::dsp::LadderFilterMode::LPF24> {};
