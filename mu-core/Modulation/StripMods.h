#pragma once

#include <atomic>
#include <cmath>
#include <limits>
#include <string_view>
#include <juce_core/juce_core.h>

// The mixer strip's Pan and FX sends (Effect, Delay, Reverb) as modulation targets. They are parameters of the layer's
// mixer strip (ch{N}_pan / sendEff / sendDly / sendRev), not of the layer, so a layer that offers them as targets resolves them with
// the rest of its table and publishes the four values here; the host hands them to the strip's per-block
// modulation overrides after the layer renders (Layer::stripMods).
namespace mu_mod
{

// True for a target that names a mixer-strip parameter (by its suffix) rather than one of the layer's own.
constexpr bool isMixerStripParam(std::string_view param) noexcept
{
    return param == "pan" || param == "sendEff" || param == "sendDly" || param == "sendRev";
}

// The id of a table row's parameter for layer `slot`: a strip parameter under "ch{N}_", any other under `layerPrefix`.
inline juce::String paramIdFor(const juce::String& layerPrefix, int slot, const char* param)
{
    return isMixerStripParam(param) ? "ch" + juce::String(slot) + "_" + param : layerPrefix + param;
}

// The resolved strip values, written by the layer on the audio thread and read by the host in the same call: pan
// (-1..+1), then the Effect, Delay and Reverb sends (0..1).
struct StripMods
{
    void publish(float pan, float effect, float delay, float reverb) noexcept
    {
        const float x[4] = { pan, effect, delay, reverb };
        for (int i = 0; i < 4; ++i)
        {
            const float lo = i == 0 ? -1.0f : 0.0f;
            v[i].store(x[i] < lo ? lo : (x[i] > 1.0f ? 1.0f : x[i]), std::memory_order_relaxed);
        }
    }
    // False until the layer has resolved once (nothing to hand over yet).
    bool read(float (&out)[4]) const noexcept
    {
        for (int i = 0; i < 4; ++i) out[i] = v[i].load(std::memory_order_relaxed);
        return ! std::isnan(out[0]);
    }

    std::atomic<float> v[4] { { std::numeric_limits<float>::quiet_NaN() }, { std::numeric_limits<float>::quiet_NaN() },
                              { std::numeric_limits<float>::quiet_NaN() }, { std::numeric_limits<float>::quiet_NaN() } };
};

} // namespace mu_mod
