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

// Depth is a percentage of the target knob's range (family standard, Modulation/ModTarget.h):
// every product seeds targets as their knob's 0..1 proportion, so the amount a depth=100%
// x source=100% assignment adds is exactly 1.0 — no per-target scale factors.

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
    ++revision;   // structural change — let UI know its cached "is assigned" flags are stale

    cachedSortOrder = getSortedOrder();

    const std::size_t n = assignments.size();
    cachedDepthKeys.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        cachedDepthKeys[i] = std::string(kMetaPrefix) + assignments[i].id + kMetaSuffix;

    // Rebuild workMap on the message thread so process() (audio thread) only ever
    // OVERWRITES existing keys — never inserts (an insert allocates a node, RT-unsafe).
    // clear() here also purges stale keys left by removed assignments. We pre-insert
    // BOTH key families process() writes each block:
    //   - every CS-output key (cs0_output..csN_output) — written from `sequences`
    //   - every assignment depth key (meta-modulation sources)
    // (A previous version cleared + re-inserted inside process(), which freed these
    // nodes every block and reallocated them on the audio thread.)
    const std::size_t needed = (std::size_t) mu_limits::kMaxControlSequences + n + 4;
    workMap.clear();
    if (workMap.bucket_count() < needed)
        workMap.reserve(needed + 16);

    for (int i = 0; i < mu_limits::kMaxControlSequences; ++i)
        workMap.emplace("cs" + std::to_string(i) + "_output", 0.0f);
    for (std::size_t i = 0; i < n; ++i)
        workMap.emplace(cachedDepthKeys[i], 0.0f);
}

void ModulationMatrix::process(const std::vector<ControlSequence>& sequences,
                               double songBeatPos,
                               std::unordered_map<std::string_view, float>& paramValues) const
{
    if (assignments.empty())
        return;

    // No clear()/insert here — rebuildCache() (message thread) pre-inserted every
    // CS-output and depth key, so these are pure overwrites: no audio-thread alloc.
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
            // Bitwig-style curve: k = 2^(curve/100), so curve=0 → k=1 (linear),
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
            // Proportion of the knob's range: depth=100% × src=100% adds 1.0 (the whole range).
            dstIt->second += srcVal * a.depth * 0.0001f;
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
