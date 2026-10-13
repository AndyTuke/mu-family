#pragma once

#include "Control/ControlSink.h"
#include <vector>

// Records every action a surface performs; `accept` is what perform() answers.
namespace mu_control::test
{

struct FakeControlSink : mu_core::ControlSink
{
    bool accept = true;
    std::vector<mu_core::ControlAction> performed;

    bool perform(const mu_core::ControlAction& a) override { performed.push_back(a); return accept; }
};

} // namespace mu_control::test
