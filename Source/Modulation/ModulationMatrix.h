#pragma once

#include "ModulationAssignment.h"
#include "../Sequencer/ControlSequence.h"
#include "../MuLimits.h"

#include <string>
#include <unordered_map>
#include <vector>

//==============================================================================
// ModulationMatrix
//
// One instance per rhythm. Owns a list of ModulationAssignments.
// process() evaluates every ControlSequence in the rhythm at the current
// song position, then applies each assignment to the caller's parameter map.
//
// Source IDs:
//   "cs0_output" ... "cs7_output" — ControlSequence outputs
//   "assign_{id}_depth"           — another assignment's depth (v2 meta-modulation)
//
// Dependency order: assignments that source a CS output have no dependencies
// on other assignments, so they sort freely. Assignments that source another
// assignment's depth come after their dependency. Circular deps are rejected
// at addAssignment() time.
//==============================================================================
class ModulationMatrix
{
public:
    static constexpr int MaxAssignments = mu_limits::kMaxModulationAssignments;

    // Adds the assignment. Returns false if it would create a circular dependency
    // or if MaxAssignments is reached.
    bool addAssignment(const ModulationAssignment& a);

    // Removes the assignment with the given ID. No-op if not found.
    void removeAssignment(const std::string& id);

    // Updates the depth of an existing assignment. No-op if not found.
    void setDepth(const std::string& id, float depth);

    // Updates the curve (-100..+100 bipolar bend) of an existing assignment. No-op if not found.
    void setCurve(const std::string& id, float curve);

    const std::vector<ModulationAssignment>& getAssignments() const { return assignments; }

    // Evaluates all ControlSequences at songBeatPos, then applies every assignment
    // to paramValues. Values not present in paramValues are silently skipped.
    // paramValues is keyed by string_view (literals only). a.destinationId is
    // std::string which converts implicitly for lookup.
    void process(const std::vector<ControlSequence>& sequences,
                 double songBeatPos,
                 std::unordered_map<std::string_view, float>& paramValues) const;

private:
    std::vector<ModulationAssignment> assignments;

    // Pre-allocated work map reused each process() call to avoid heap allocation
    // on the audio thread.  process() is audio-thread-only, so this is safe.
    mutable std::unordered_map<std::string, float> workMap;

    // Cached on the message thread (under modLock) every time assignments change.
    // Read on the audio thread inside process() — protected by the same modLock.
    std::vector<int>         cachedSortOrder;      // topological indices into assignments
    std::vector<std::string> cachedDepthKeys;       // "assign_" + a.id + "_depth" per assignment

    // Rebuilds cachedSortOrder and cachedDepthKeys. Call after any mutation to assignments.
    void rebuildCache();

    // Returns assignment indices in topological processing order.
    std::vector<int> getSortedOrder() const;

    // Returns true if adding 'candidate' would create a cycle.
    bool wouldCreateCycle(const ModulationAssignment& candidate) const;

    // Helpers for parsing meta-modulation source IDs.
    static bool isMetaSource(const std::string& sourceId, std::string& outDepId);
};
