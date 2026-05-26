#pragma once

#include "InsertAlgorithmBase.h"

// driveChar = 0. No-op — keeps the algorithms[] dispatch table dense so
// InsertProcessor::process() never has to branch around a null slot.
class NoneInsert : public InsertAlgorithmBase
{
public:
    void prepare(double, int) override {}
    void reset()                 override {}
    void process(juce::AudioBuffer<float>&, int, int, const VoiceParams&, float&) override {}
};
