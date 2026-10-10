#pragma once

#include "Control/ControlAction.h"

// What performs a ControlAction. Message thread only. ProcessorBase implements it for the whole
// family, so every product answers every surface the same way.
namespace mu_core
{

class ControlSink
{
public:
    virtual ~ControlSink() = default;

    // Returns false when the action is not supported (yet) or its target no longer exists.
    virtual bool perform(const ControlAction& action) = 0;
};

} // namespace mu_core
