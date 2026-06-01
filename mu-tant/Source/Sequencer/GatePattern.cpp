#include "GatePattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mu_tant
{

namespace
{
    inline float clamp01(float x) noexcept
    {
        return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    }

    // Bend a 0..1 line via a power curve. bend > 0 bulges it up (sustains
    // higher than linear); bend < 0 bulges it down (moves away faster). The
    // 2^(-bend*2) mapping gives a usable 0.25..4 exponent range across
    // [+1, -1] without singularities at the extremes.
    inline float bend(float x, float amount) noexcept
    {
        if (amount == 0.0f) return x;
        const float exponent = std::pow(2.0f, -amount * 2.0f);
        return std::pow(clamp01(x), exponent);
    }
}

bool GateEnvelope::playsOnLoop(int loopCount) const noexcept
{
    // Loop mask: fire only if the bit for the current position in the M-loop
    // cycle is set. Bit 0 = position 0 (first loop in cycle), etc.
    const int m = loopM < 1 ? 1 : loopM;
    const int pos = loopCount % m;
    if (!((loopMask >> pos) & 1u))
        return false;

    // Probability: deterministic per-(loop, startCell) hash so the decision is
    // stable across the duration of one loop and different each loop.
    if (probability < 1.0f)
    {
        uint32_t h = (uint32_t) loopCount * 2654435761u ^ (uint32_t) startCell * 40503u;
        h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
        const float r = (float) (h & 0xFFFFFFu) / (float) 0x1000000u;
        if (r >= probability) return false;
    }
    return true;
}

float GateEnvelope::shape(float p) const noexcept
{
    const float s = clamp01(split);
    if (p <= s)
    {
        if (s <= 0.0f) return 1.0f;        // instant attack — already at peak
        return bend(p / s, attackBend);    // attack rises 0 -> 1 over [0, s]
    }
    if (s >= 1.0f) return 1.0f;            // pure attack — full until the end
    const float d = (p - s) / (1.0f - s);  // 0..1 across the decay span
    return bend(1.0f - d, decayBend);       // decay falls 1 -> 0
}

float GateEnvelope::value(float phase01, float gap01) const noexcept
{
    phase01 = clamp01(phase01);
    const float gap    = clamp01(gap01);
    const float active = 1.0f - gap;          // leading fraction the shape fills
    if (active <= 0.0f) return 0.0f;          // gap = 100% -> whole region silent
    if (gap > 0.0f && phase01 >= active) return 0.0f;   // trailing silence tail

    float p = phase01 / active;               // remap region-phase into [0,1]
    if (p > 1.0f) p = 1.0f;
    if (reverse) p = 1.0f - p;                // mirror -> swaps attack and decay
    return shape(p);
}

int GatePattern::totalCells() const noexcept
{
    return kTotalBars * static_cast<int>(subdivision);
}

const GateEnvelope* GatePattern::envelopeAtCell(int cellIdx) const noexcept
{
    // Linear scan — typical patterns have <= 64 cells and few envelopes.
    for (const auto& env : envelopes)
        if (env.covers(cellIdx)) return &env;
    return nullptr;
}

void GatePattern::sortEnvelopes()
{
    std::sort(envelopes.begin(), envelopes.end(),
              [](const GateEnvelope& a, const GateEnvelope& b) { return a.startCell < b.startCell; });
}

GateEnvelope* GatePattern::addEnvelope(const GateEnvelope& env)
{
    const int newStart = env.startCell;
    const int newEnd   = env.startCell + env.lengthCells;   // exclusive

    // Drop any existing envelope whose region overlaps the new one so the set
    // stays non-overlapping.
    envelopes.erase(
        std::remove_if(envelopes.begin(), envelopes.end(),
                       [newStart, newEnd](const GateEnvelope& e)
                       {
                           const int eStart = e.startCell;
                           const int eEnd   = e.startCell + e.lengthCells;
                           return eStart < newEnd && newStart < eEnd;   // ranges intersect
                       }),
        envelopes.end());

    envelopes.push_back(env);
    sortEnvelopes();
    hasEnvelopes.store(true, std::memory_order_relaxed);

    for (auto& e : envelopes)
        if (e.startCell == newStart) return &e;
    return nullptr;
}

void GatePattern::removeEnvelopeCovering(int cellIdx)
{
    envelopes.erase(
        std::remove_if(envelopes.begin(), envelopes.end(),
                       [cellIdx](const GateEnvelope& e) { return e.covers(cellIdx); }),
        envelopes.end());
    hasEnvelopes.store(!envelopes.empty(), std::memory_order_relaxed);
}

void GatePattern::mergeRange(int firstCell, int lastCell)
{
    if (lastCell < firstCell) std::swap(firstCell, lastCell);
    firstCell = std::max(0, firstCell);
    const int cells = totalCells();
    if (cells <= 0) return;
    lastCell = std::min(cells - 1, lastCell);
    if (lastCell < firstCell) return;

    const int rangeEnd = lastCell + 1;   // exclusive

    // Average the shape of every envelope that intersects the dragged range.
    float sumSplit = 0.0f, sumAtk = 0.0f, sumDec = 0.0f;
    int   merged   = 0;
    for (const auto& e : envelopes)
    {
        const int eStart = e.startCell;
        const int eEnd   = e.startCell + e.lengthCells;
        if (eStart < rangeEnd && firstCell < eEnd)   // intersects [firstCell, rangeEnd)
        {
            sumSplit += e.split;
            sumAtk   += e.attackBend;
            sumDec   += e.decayBend;
            ++merged;
        }
    }

    GateEnvelope env;
    env.startCell   = firstCell;
    env.lengthCells = rangeEnd - firstCell;
    if (merged > 0)
    {
        env.split      = sumSplit / (float) merged;
        env.attackBend = sumAtk   / (float) merged;
        env.decayBend  = sumDec   / (float) merged;
    }
    // addEnvelope trims every overlapping envelope, then inserts the merged one.
    addEnvelope(env);
}

float GatePattern::gateAt(double beatPos, float gap01, int loopCount) const noexcept
{
    // Empty pattern -> no gating, continuous drone.
    if (envelopes.empty()) return 1.0f;

    const int cells = totalCells();
    if (cells <= 0) return 1.0f;

    // 2 bars in 4/4 = 8 beats. Wrap the absolute beat into the pattern span.
    const double patBeats = (double) kTotalBars * 4.0;
    double pos = std::fmod(beatPos, patBeats);
    if (pos < 0.0) pos += patBeats;

    const double cellLen = patBeats / (double) cells;
    int cell = (int) (pos / cellLen);
    if (cell < 0)         cell = 0;
    if (cell > cells - 1) cell = cells - 1;

    // Re-scan for the covering envelope only when the cell changes.
    if (cell != cachedCell)
    {
        cachedCell = cell;
        cachedEnv  = envelopeAtCell(cell);
    }
    if (cachedEnv == nullptr) return 0.0f;   // cell with no envelope -> silent

    // Probability / loop-N-of-M gate: envelope suppressed this loop → silent.
    if (!cachedEnv->playsOnLoop(loopCount)) return 0.0f;

    // Phase across the covering envelope's full region (not just this cell).
    const double regionStart = (double) cachedEnv->startCell * cellLen;
    const double regionLen   = (double) cachedEnv->lengthCells * cellLen;
    const float phase = (float) ((pos - regionStart) / regionLen);
    return cachedEnv->value(phase, gap01);
}

GateMode gateModeFor(bool bypassed, bool playing, bool patternEmpty) noexcept
{
    if (bypassed)     return GateMode::Pass;      // audition — raw drone passes
    if (! playing)    return GateMode::Silence;   // stopped → gate closed
    if (patternEmpty) return GateMode::Silence;   // nothing drawn → nothing passes
    return GateMode::Envelope;
}

void applyGateBlock(GatePattern& pattern, float* left, float* right, int numSamples,
                    float gap01, bool bypassed, bool playing,
                    double beatStart, double beatsPerSample, double sampleRate,
                    int loopCount) noexcept
{
    const GateMode mode = gateModeFor(bypassed, playing,
                                       !pattern.hasEnvelopes.load(std::memory_order_relaxed));

    if (mode == GateMode::Pass)
        return;                                    // leave the buffer untouched

    if (mode == GateMode::Silence)
    {
        pattern.gateLevel = 0.0f;                  // gate fully closed; re-open ramps from 0
        for (int i = 0; i < numSamples; ++i)
        {
            if (left)  left[i]  = 0.0f;
            if (right) right[i] = 0.0f;
        }
        return;
    }

    // Max gate rise per sample — caps an instant (0-attack) open to kMinAttackMs
    // so it ramps instead of clicking. Falling edges follow the envelope exactly
    // (drawn decays are preserved); attacks drawn slower than the cap pass through
    // unchanged. sampleRate <= 0 disables the slew (rise == full scale).
    const float maxRise = (sampleRate > 0.0 && GatePattern::kMinAttackMs > 0.0f)
                        ? (float) (1.0 / ((double) GatePattern::kMinAttackMs * 0.001 * sampleRate))
                        : 1.0f;

    // Envelope — tryLock so a concurrent UI edit can't tear the envelope vector
    // mid-read. On contention, leave the block as-is (a brief, edit-time blip).
    bool expected = false;
    if (pattern.editLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
    {
        pattern.resetGateCache();
        for (int i = 0; i < numSamples; ++i)
        {
            const float target = pattern.gateAt(beatStart + beatsPerSample * (double) i, gap01, loopCount);
            // Slew-limit the rising edge only; fall instantly so decay shapes are exact.
            pattern.gateLevel = (target > pattern.gateLevel)
                              ? std::min(target, pattern.gateLevel + maxRise)
                              : target;
            const float g = pattern.gateLevel;
            if (left)  left[i]  *= g;
            if (right) right[i] *= g;
        }
        pattern.editLock.store(false, std::memory_order_release);
    }
}

} // namespace mu_tant
