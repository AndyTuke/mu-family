#include "ModulationMatrix.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <unordered_set>

//==============================================================================
// Static helpers
//==============================================================================

static constexpr auto kMetaPrefix = "assign_";
static constexpr auto kMetaSuffix = "_depth";

// Per-destination full-swing magnitude in the same units as paramValues. At depth=100%
// and CS output = 100% the destination is offset by ±this value. Defaults to 100 for
// destinations that already operate on a 0-100 display scale (amp/filter ADSR, etc.).
static float depthScaleFor(const std::string& destId)
{
    // #216d: Hz-domain destinations now apply multiplicatively in semitones
    // (see isLogHzDest + the apply branch below). Full-swing scale is 48 semis
    // = ±4 octaves, matching filterEnvDepth — keeps the same depth sweeping
    // the same number of octaves whether the base cutoff is 100 Hz or 10 kHz.
    if (destId == "filter.cutoff" || destId == "insert.lpf") return 48.0f;
    // Semitones / dB / bits — match the destination's natural full range.
    if (destId == "pitch.semitones") return 24.0f;   // ±24 semitones = ±2 oct full swing (#218 collapse)
    // pitch.octave / pitch.fine deprecated by #218 — destinations removed from UI; legacy
    // assignments fall through silently because paramValues no longer holds these keys.
    if (destId == "fenv.depth")      return 48.0f;   // 0..48 semitones (full range)
    if (destId == "pitch.envDepth")  return 24.0f;   // 0..24 semitones (#223)
    if (destId == "amp.level")       return 2.0f;    // 0..2 gain = -inf..+6 dB (#223)
    if (destId == "accentDb")        return 12.0f;   // 0..12 dB (#223)
    if (destId == "insert.output")   return 24.0f;   // -24..0 dB (full range)
    if (destId == "insert.bits")     return 16.0f;   // 1..16 bits (full range)
    // Pattern destinations.
    if (destId == "euclid.a.hits"   || destId == "euclid.b.hits"
     || destId == "euclid.a.rotate" || destId == "euclid.b.rotate"
     || destId == "euclid.c.hits"   || destId == "euclid.c.rotate") return 16.0f;
    return 100.0f;  // 0-100 display-scale default
}

// #216d: true for Hz-domain destinations where modulation must be multiplicative-in-
// octaves rather than additive-in-Hz (so a fixed depth sweeps the same octave range
// whether the base cutoff is 100 Hz or 10 kHz). Matches the filterEnvDepth model in
// VoiceEngine: `cutoff * 2^(semis/12)`.
static bool isLogHzDest(const std::string& destId)
{
    return destId == "filter.cutoff" || destId == "insert.lpf";
}

bool ModulationMatrix::isMetaSource(const std::string& src, std::string& outDepId)
{
    const std::size_t prefixLen = std::strlen(kMetaPrefix);
    const std::size_t suffixLen = std::strlen(kMetaSuffix);

    if (src.size() <= prefixLen + suffixLen)
        return false;
    if (src.compare(0, prefixLen, kMetaPrefix) != 0)
        return false;
    if (src.compare(src.size() - suffixLen, suffixLen, kMetaSuffix) != 0)
        return false;

    outDepId = src.substr(prefixLen, src.size() - prefixLen - suffixLen);
    return true;
}

//==============================================================================
// Public interface
//==============================================================================

bool ModulationMatrix::addAssignment(const ModulationAssignment& a)
{
    if (static_cast<int>(assignments.size()) >= MaxAssignments)
        return false;

    if (wouldCreateCycle(a))
        return false;

    assignments.push_back(a);
    rebuildCache();
    return true;
}

void ModulationMatrix::removeAssignment(const std::string& id)
{
    assignments.erase(
        std::remove_if(assignments.begin(), assignments.end(),
            [&id](const ModulationAssignment& a) { return a.id == id; }),
        assignments.end());
    rebuildCache();
}

void ModulationMatrix::setDepth(const std::string& id, float depth)
{
    for (auto& a : assignments)
        if (a.id == id) { a.depth = depth; return; }
}

void ModulationMatrix::setCurve(const std::string& id, float curve)
{
    for (auto& a : assignments)
        if (a.id == id) { a.curve = curve; return; }
}

void ModulationMatrix::rebuildCache()
{
    cachedSortOrder = getSortedOrder();

    const std::size_t n = assignments.size();
    cachedDepthKeys.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        cachedDepthKeys[i] = std::string(kMetaPrefix) + assignments[i].id + kMetaSuffix;

    // Pre-size workMap so process() never rehashes (8 CS + n assignments + headroom).
    const std::size_t needed = 8 + n + 4;
    if (workMap.bucket_count() < needed)
        workMap.reserve(needed + 16);
}

void ModulationMatrix::process(const std::vector<ControlSequence>& sequences,
                               double songBeatPos,
                               std::unordered_map<std::string, float>& paramValues) const
{
    if (assignments.empty())
        return;

    // workMap is pre-sized in rebuildCache(); no reserve needed here.
    workMap.clear();

    // CS output keys are short ("cs0_output" = 10 chars) and fit in SSO on all platforms.
    for (const auto& cs : sequences)
        workMap[cs.id + "_output"] = cs.evaluate(songBeatPos);

    // Use pre-computed keys to avoid heap allocation for long UUID-based assignment IDs.
    for (std::size_t i = 0; i < assignments.size(); ++i)
        workMap[cachedDepthKeys[i]] = assignments[i].depth;

    for (int idx : cachedSortOrder)
    {
        const auto& a = assignments[idx];

        auto srcIt = workMap.find(a.sourceId);
        if (srcIt == workMap.end())
            continue;

        auto dstIt = paramValues.find(a.destinationId);
        if (dstIt != paramValues.end())
        {
            // srcIt->second ∈ [-100..+100], a.depth ∈ [-100..+100].
            // #224 Bitwig-style curve: k = 2^(curve/100), so curve=0 → k=1 (linear),
            // curve=+100 → k=2 (square, exp-like), curve=-100 → k=0.5 (square-root,
            // log-like). Sign-preserving so bipolar sources stay bipolar. Skipped
            // when curve == 0 (the common case) so the per-step cost is one
            // float compare for assignments that don't use the curve.
            float srcVal = srcIt->second;
            if (a.curve != 0.0f)
            {
                const float k        = std::pow(2.0f, a.curve / 100.0f);
                const float mag      = std::abs(srcVal) / 100.0f;
                const float bent     = std::pow(mag, k);
                srcVal = (srcVal < 0.0f ? -bent : bent) * 100.0f;
            }
            // Scale by the destination's full-swing magnitude so depth=100% × src=100%
            // produces a musically meaningful sweep regardless of the param's units.
            const float scale = depthScaleFor(a.destinationId);
            const float amount = srcVal * a.depth * scale * 0.0001f;
            if (isLogHzDest(a.destinationId))
                // #216d: multiplicative in semitones — keeps a fixed depth sweeping
                // the same number of octaves regardless of base cutoff.
                dstIt->second *= std::pow(2.0f, amount / 12.0f);
            else
                dstIt->second += amount;
        }
    }
}

//==============================================================================
// Private — topological sort (Kahn's algorithm)
//==============================================================================

std::vector<int> ModulationMatrix::getSortedOrder() const
{
    const int n = static_cast<int>(assignments.size());
    std::vector<int> inDegree(n, 0);
    std::vector<std::vector<int>> dependents(n); // dependents[j] = indices that depend on j

    for (int i = 0; i < n; ++i)
    {
        std::string depId;
        if (!isMetaSource(assignments[i].sourceId, depId))
            continue;

        for (int j = 0; j < n; ++j)
        {
            if (assignments[j].id == depId)
            {
                dependents[j].push_back(i);
                ++inDegree[i];
            }
        }
    }

    std::queue<int> q;
    for (int i = 0; i < n; ++i)
        if (inDegree[i] == 0)
            q.push(i);

    std::vector<int> order;
    order.reserve(n);
    while (!q.empty())
    {
        int cur = q.front(); q.pop();
        order.push_back(cur);
        for (int dep : dependents[cur])
            if (--inDegree[dep] == 0)
                q.push(dep);
    }

    // Cycle guard (shouldn't happen; addAssignment prevents it).
    if (static_cast<int>(order.size()) < n)
    {
        order.clear();
        for (int i = 0; i < n; ++i)
            order.push_back(i);
    }

    return order;
}

//==============================================================================
// Private — cycle detection (DFS from the candidate's dependency)
//==============================================================================

bool ModulationMatrix::wouldCreateCycle(const ModulationAssignment& candidate) const
{
    std::string depId;
    if (!isMetaSource(candidate.sourceId, depId))
        return false; // CS output — leaves in the graph, never cyclic

    // Check whether candidate.id is reachable from depId by following meta-sources.
    std::unordered_set<std::string> visited;
    std::queue<std::string> q;
    q.push(depId);

    while (!q.empty())
    {
        const std::string cur = q.front(); q.pop();

        if (cur == candidate.id)
            return true;

        if (!visited.insert(cur).second)
            continue;

        for (const auto& a : assignments)
        {
            if (a.id != cur)
                continue;
            std::string nextDep;
            if (isMetaSource(a.sourceId, nextDep))
                q.push(nextDep);
        }
    }

    return false;
}
