#pragma once

#include "FilterFamilies.h"

// type 0 — 12 dB/oct lowpass via JUCE's StateVariableTPTFilter.
class Lp12Filter : public SvfFilter12<juce::dsp::StateVariableTPTFilterType::lowpass> {};
