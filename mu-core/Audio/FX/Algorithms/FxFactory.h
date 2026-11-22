#pragma once

#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/EffectAlgorithmBase.h"

#include <memory>

namespace mu_audio
{
// The one place an algorithm is registered into a host. The static_assert makes registering a class in a role its kUse
// does not allow a compile error, so the insert and send tables cannot drift from the class's own statement.

// A send-bus Effect slot algorithm (EffectSlot).
template <class A>
std::unique_ptr<EffectAlgorithmBase> makeSend()
{
    static_assert(supports(A::kUse, FxUse::Send), "this algorithm is not marked for the send bus");
    return std::make_unique<A>();
}

// A per-voice insert algorithm (InsertProcessor).
template <class A>
std::unique_ptr<InsertAlgorithmBase> makeInsert()
{
    static_assert(supports(A::kUse, FxUse::Insert), "this algorithm is not marked for the insert");
    return std::make_unique<A>();
}
}  // namespace mu_audio
