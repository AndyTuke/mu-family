#pragma once

#include "FilterFamilies.h"

// type 1 — 12 dB/oct highpass via JUCE's StateVariableTPTFilter.
class Hp12Filter : public SvfFilter12<juce::dsp::StateVariableTPTFilterType::highpass> {};
