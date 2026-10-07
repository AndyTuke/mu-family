#pragma once

#include "FilterFamilies.h"

// type 2 — 12 dB/oct bandpass via JUCE's StateVariableTPTFilter.
class Bp12Filter : public SvfFilter12<juce::dsp::StateVariableTPTFilterType::bandpass> {};
