#include "GatePattern.h"

#include <algorithm>
#include <cmath>

namespace mu_tant
{

float GateEnvelope::value(float phase01) const noexcept
{
    // Clamp phase outside [0, 1] to its boundary value so envelopes evaluated
    // beyond their own span (audio-thread arithmetic round-off) saturate
    // cleanly instead of producing garbage > 1 or negative.
    if (phase01 <= 0.0f) return options.reverse ? 0.0f : 1.0f;
    if (phase01 >= 1.0f) return options.reverse ? 1.0f : 0.0f;

    // Default decay shape: linear 1.0 → 0.0 over [0, 1].
    float linearDecay = 1.0f - phase01;
    if (options.reverse) linearDecay = phase01;     // attack 0 → 1

    // Curve bend in [-1, +1] reshapes the decay via power curve.
    // For x in [0,1]: pow(x, e) < x when e > 1 (faster decay), > x when e < 1.
    //   bend < 0 (concave) → exponent > 1 → faster initial drop, audible gap
    //   bend > 0 (convex)  → exponent < 1 → sustains near 1, drops at end
    //   bend = 0           → exponent 1 (linear)
    // Empirical mapping: exponent = 2^(-bend * 2) gives a usable 0.25..4
    // range across [+1, -1] without singularities at the extremes.
    if (curveBend == 0.0f) return linearDecay;
    const float exponent = std::pow(2.0f, -curveBend * 2.0f);
    return std::pow(linearDecay, exponent);
}

int GatePattern::totalCells() const noexcept
{
    return kTotalBars * static_cast<int>(subdivision);
}

const GateEnvelope* GatePattern::cellEnvelope(int cellIdx) const noexcept
{
    // Direct match — one envelope per cell. Linear scan is fine: typical
    // patterns have ≤ 64 cells, the loop fits comfortably in audio-thread
    // budget at 30 evaluations per audio block.
    for (const auto& env : envelopes)
        if (env.cell == cellIdx) return &env;
    return nullptr;
}

GateEnvelope* GatePattern::addOrReplaceEnvelope(const GateEnvelope& env)
{
    // Drop any prior envelope at the same cell — design spec is "one
    // envelope per cell"; adding overlaps just replaces.
    for (auto it = envelopes.begin(); it != envelopes.end(); ++it)
    {
        if (it->cell == env.cell)
        {
            *it = env;
            return &(*it);
        }
    }
    envelopes.push_back(env);
    // Keep sorted by cell so envelopeSpan() can find "next" cheaply.
    std::sort(envelopes.begin(), envelopes.end(),
              [](const GateEnvelope& a, const GateEnvelope& b) { return a.cell < b.cell; });
    // After sort, the pointer to the just-inserted entry needs a fresh lookup.
    for (auto& e : envelopes)
        if (e.cell == env.cell) return &e;
    return nullptr;
}

void GatePattern::removeEnvelopeAt(int cellIdx)
{
    envelopes.erase(
        std::remove_if(envelopes.begin(), envelopes.end(),
                       [cellIdx](const GateEnvelope& e) { return e.cell == cellIdx; }),
        envelopes.end());
}

int GatePattern::envelopeSpan(int envIdx) const noexcept
{
    if (envIdx < 0 || envIdx >= (int) envelopes.size()) return 0;
    const int startCell = envelopes[(size_t) envIdx].cell;
    const int endCell   = (envIdx + 1 < (int) envelopes.size())
                              ? envelopes[(size_t) envIdx + 1].cell
                              : totalCells();
    return endCell - startCell;
}

} // namespace mu_tant
