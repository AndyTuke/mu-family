#pragma once

#include "InsertAlgorithmBase.h"
#include "FxUse.h"

// driveChar = 0. No-op — keeps the algorithms[] dispatch table dense so
// InsertProcessor::process() never has to branch around a null slot.
class NoneInsert : public InsertAlgorithmBase
{
public:
    static constexpr mu_audio::FxUse kUse = mu_audio::FxUse::Insert;

    void prepare(double, int) override {}
    void reset()                 override {}
    void process(juce::AudioBuffer<float>&, int, int, const VoiceParams&, float&) override {}
};
