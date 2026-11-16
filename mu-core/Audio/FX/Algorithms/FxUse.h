#pragma once

namespace mu_audio
{
// Which hosts an FX algorithm class can run in: the per-voice insert, the send-bus Effect slot, or both. Each algorithm class
// states it once as `static constexpr mu_audio::FxUse kUse`; the two registries check it at compile time (FxFactory.h).
enum class FxUse : unsigned { Insert = 1, Send = 2, Both = 3 };

// True when an algorithm marked `have` can serve the role `want`.
constexpr bool supports(FxUse have, FxUse want) noexcept
{
    return (static_cast<unsigned>(have) & static_cast<unsigned>(want)) == static_cast<unsigned>(want);
}
}  // namespace mu_audio
