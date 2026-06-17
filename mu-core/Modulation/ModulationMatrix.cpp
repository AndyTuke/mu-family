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

// ── Modulation destination depth-scale registry ───────────────────────────────
// Formula (in process()): amount = srcVal [-100,+100] * depth [-100,+100] * scale * 0.0001
// At depth=100%, srcVal=100%: amount = scale. `scale` is the max useful additive offset in
// the units the product seeds into paramValues (the family "50% mod → 50% knob turn" rule).
//
// mu-core stays plugin-agnostic: it knows only (1) the generic `.prop` convention — any id
// ending ".prop" carries a 0..1 proportion (scale 1.0), so the product seeds/writes back via
// each slider's NormalisableRange and needs no entry here (mu-on uses this for everything);
// (2) the shared voice/filter/insert vocabulary that mu-core's own VoiceEngine + filter +
// InsertSubsection define (seeded below); (3) a 100 default for any 0..100 display dest.
// Product-ENGINE-specific destinations (mu-tant `osc1.*`, mu-clid `euclid.*`/`ks.*`/`voc.*`)
// are registered by each product at static-init via ModulationMatrix::registerDepthScale, so
// the shared platform no longer enumerates plugin param ids.
static std::unordered_map<std::string, float>& depthScaleRegistry()
{
    // Born with mu-core's shared voice/filter/insert scales; products append their own.
    static std::unordered_map<std::string, float> registry = []
    {
        return std::unordered_map<std::string, float>{
            // Proportion-space shared-voice dests (seed = slider proportion → scale 1.0).
            { "filter.cutoff", 1.0f }, { "filter.lowCut", 1.0f },
            { "amp.attack",   1.0f },  { "amp.decay",    1.0f },
            { "fenv.attack",  1.0f },  { "fenv.decay",   1.0f },
            // Additive-in-display shared-voice dests (scale = full slider range).
            { "pitch.semitones", 24.0f }, { "pitch.octave", 72.0f },
            { "fenv.depth", 48.0f },      { "pitch.envDepth", 24.0f },
            { "accentDb", 12.0f },        { "amp.level", 66.0f },
            { "filter.resonance", 0.99f },
            // Shared insert slots — values stored NORMALISED 0..1, so a full-depth mod must
            // span 0..1 → scale 1.0 (the default 100 would saturate them to a binary on/off).
            { "insert.output", 24.0f }, { "insert.bits", 1.0f },
            { "insert.p1", 1.0f }, { "insert.p2", 1.0f }, { "insert.p3", 1.0f }, { "insert.p4", 1.0f },
        };
    }();
    return registry;
}

// Per-destination full-swing magnitude in the same units as paramValues. `.prop` → 1.0;
// otherwise the registered scale (mu-core generics + product registrations); else 100.
static float depthScaleFor(const std::string& destId)
{
    if (destId.size() >= 5 && destId.compare(destId.size() - 5, 5, ".prop") == 0)
        return 1.0f;
    const auto& registry = depthScaleRegistry();
    const auto it = registry.find(destId);
    return it != registry.end() ? it->second : 100.0f;   // 0-100 display-scale default
}

void ModulationMatrix::registerDepthScale(const std::string& destId, float scale)
{
    depthScaleRegistry()[destId] = scale;
}

// true for Hz-domain destinations where modulation must be multiplicative-in-
// octaves rather than additive-in-Hz (so a fixed depth sweeps the same octave range
// whether the base cutoff is 100 Hz or 10 kHz). Matches the filterEnvDepth model in
// VoiceEngine: `cutoff * 2^(semis/12)`.
static bool isLogHzDest(const std::string& destId)
{
    // Currently empty — filter.cutoff switched to proportion-space modulation. Function
    // kept for future destinations that may want multiplicative-in-octaves behaviour.
    (void) destId;
    return false;
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
            // Scale by the destination's full-swing magnitude so depth=100% × src=100%
            // produces a musically meaningful sweep regardless of the param's units.
            const float scale = depthScaleFor(a.destinationId);
            const float amount = srcVal * a.depth * scale * 0.0001f;
            if (isLogHzDest(a.destinationId))
                // multiplicative in semitones — keeps a fixed depth sweeping
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
