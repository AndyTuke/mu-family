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

// ── Modulation destination reference table ────────────────────────────────────
// Formula: amount = srcVal [-100,+100] * depth [-100,+100] * scale * 0.0001
// At depth=100%, srcVal=100%: amount = scale. Scale = max useful additive offset
// in the units stored in modParamValues. Log-Hz destinations (filter.cutoff, insert.lpf)
// are multiplicative (semitone offset); all others are additive.
//
//  Destination         paramValues units   Range           Scale   Notes
//  ──────────────────  ──────────────────  ──────────────  ──────  ─────────────────────────────
//  amp.attack          display 0-100       0..100          100     full-range additive
//  amp.decay           display 0-100       0..100          100     full-range additive
//  amp.sustain         display 0-100       0..100          100     full-range additive
//  amp.release         display 0-100       0..100          100     full-range additive
//  amp.level           gain 0-2            0..2            2       0=silent, 2=double (#223)
//  accentDb            dB 0-12             0..12           12      (#223)
//  filter.cutoff       Hz (log-mult)       20..20000 Hz    12      ±1 octave (#291, reduced from 48)
//  filter.resonance    display 0-100       0..100          100     full-range additive
//  fenv.attack         display 0-100       0..100          100     full-range additive
//  fenv.decay          display 0-100       0..100          100     full-range additive
//  fenv.depth          semitones 0-48      0..48           48      full-range additive
//  pitch.semitones     semitones 0 base    ±12 st          12      ±1 octave
//  pitch.octave        semitones 0 base    ±36 st          36      ±3 octaves
//  pitch.envDepth      semitones 0-24      0..24           24      (#223)
//  insert.drive        display 0-100       0..100          100     full-range additive
//  insert.output       dB -24..0           -24..0 dB       24      full-range additive
//  insert.bits         actual bits 1-16    1..16 bits      1       ±1 bit at full depth (#266)
//  insert.rate         log-norm 0-100      0..100          100     full-range additive (log space)
//  insert.dither       display 0-100       0..100          100     full-range additive
//  insert.lpf          Hz (log-mult)       20..20000 Hz    48      ±4 octaves (#291)
//  euclid.*.hits       steps 0-64          0..64           16      ±16 steps (~25% of max)
//  euclid.*.rotate     steps 0-63          0..63           16      ±16 steps
//  euclid.*.prePad     steps 0-12          0..12           12      full pre-pad range
//  euclid.*.postPad    steps 0-12          0..12           12      full post-pad range
//  euclid.*.insSt      steps 0-63          0..63           16      ±16 positions
//  euclid.*.insLen     steps 0-8           0..8             8      full insert-len range

// Per-destination full-swing magnitude in the same units as paramValues. At depth=100%
// and CS output = 100% the destination is offset by ±this value. Defaults to 100 for
// destinations that already operate on a 0-100 display scale (amp/filter ADSR, etc.).
static float depthScaleFor(const std::string& destId)
{
    // Hz-domain destinations apply multiplicatively in semitones (see isLogHzDest).
    // Scale reduced 48→12 (±1 octave at full depth) so that small LFO amounts give
    // subtle movement — at 48 even depth=5 caused >2 semitones which is too wide for
    // narrow-band filters (notch). depth=5→0.6 semi, depth=25→3 semi, depth=100→12 semi.
    if (destId == "filter.cutoff" || destId == "insert.lpf") return 12.0f;
    // Semitones / dB / bits — match the destination's natural full range.
    if (destId == "pitch.semitones") return 12.0f;   // ±12 semitones full swing
    if (destId == "pitch.octave")    return 36.0f;   // ±3 octaves = ±36 semitones full swing
    if (destId == "fenv.depth")      return 48.0f;   // 0..48 semitones (full range)
    if (destId == "pitch.envDepth")  return 24.0f;   // 0..24 semitones (#223)
    if (destId == "amp.level")       return 2.0f;    // 0..2 gain = -inf..+6 dB (#223)
    if (destId == "accentDb")        return 12.0f;   // 0..12 dB (#223)
    if (destId == "insert.output")   return 24.0f;   // -24..0 dB (full range)
    if (destId == "insert.bits")     return 1.0f;    // 1..16 bits; ±1 bit at full depth (#266)
    // algorithm-specific insert destinations. Scale equals the
    // full-swing range for the integer field so depth=100% × src=100% spans the
    // whole knob (e.g. ks.note covers all 7 chromatic notes).
    if (destId == "ks.note")         return 6.0f;    // Karplus note 0..6 (C..B)
    if (destId == "ks.octave")       return 3.0f;    // Karplus octave 0..3
    if (destId == "voc.note")        return 6.0f;    // Vocoder note 0..6 (C..B)
    if (destId == "voc.octave")      return 4.0f;    // Vocoder octave 1..5 (range 4)
    if (destId == "voc.unison")      return 6.0f;    // Vocoder unison index 0..6
    // Pattern destinations — hits/rotate. Halved from 16→8 (~half of typical 16-step
    // pattern at full depth) so user-facing depth control feels less saturated.
    if (destId == "euclid.a.hits"   || destId == "euclid.b.hits"   || destId == "euclid.c.hits"
     || destId == "euclid.a.rotate" || destId == "euclid.b.rotate" || destId == "euclid.c.rotate")
        return 8.0f;   // ±8 steps at full depth
    // Pattern destinations — pad knobs. Halved 12→6 / 8→4 for the same reason.
    if (destId == "euclid.a.prePad"  || destId == "euclid.b.prePad"  || destId == "euclid.c.prePad"
     || destId == "euclid.a.postPad" || destId == "euclid.b.postPad" || destId == "euclid.c.postPad")
        return 6.0f;   // ±6 steps at full depth (half of 12-step pad range)
    if (destId == "euclid.a.insLen"  || destId == "euclid.b.insLen"  || destId == "euclid.c.insLen")
        return 4.0f;   // ±4 steps at full depth (half of 8-step insert range)
    if (destId == "euclid.a.insSt"   || destId == "euclid.b.insSt"   || destId == "euclid.c.insSt")
        return 8.0f;   // ±8 positions at full depth (half of original 16)
    return 100.0f;  // 0-100 display-scale default
}

// true for Hz-domain destinations where modulation must be multiplicative-in-
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
                               std::unordered_map<std::string_view, float>& paramValues) const
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
