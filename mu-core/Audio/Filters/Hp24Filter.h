#pragma once

#include "FilterFamilies.h"

// type 5 — 24 dB/oct highpass via JUCE's LadderFilter (Moog-style).
class Hp24Filter : public LadderFilter24<juce::dsp::LadderFilterMode::HPF24> {};
