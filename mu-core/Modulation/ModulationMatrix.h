#pragma once

#include "ModulationAssignment.h"
#include "Sequencer/ControlSequence.h"
#include "MuLimits.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

//==============================================================================
// ModulationMatrix
//
// One instance per channel. Owns a list of ModulationAssignments.
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

    // Moves an existing assignment to a new destination, keeping its source, depth and curve. Returns false
    // (and leaves the old assignment untouched) if it is not found or the new one would form a cycle.
    bool retargetAssignment(const std::string& id, const std::string& newDestinationId);

    // Removes the assignment with the given ID. No-op if not found.
    void removeAssignment(const std::string& id);

    // Updates the depth of an existing assignment. No-op if not found.
    void setDepth(const std::string& id, float depth);

    // Updates the curve (-100..+100 bipolar bend) of an existing assignment. No-op if not found.
    void setCurve(const std::string& id, float curve);

    const std::vector<ModulationAssignment>& getAssignments() const { return assignments; }

    // Monotonic counter bumped on every structural change to the assignment list
    // (add / remove → rebuildCache). The set of assigned destinations only changes
    // when this does, so UI polling the "is this destination modulated?" flag can
    // compare against a cached value and skip re-scanning the list when unchanged.
    int assignmentsRevision() const noexcept { return revision; }

    // The assigned destinations as of the last structural change, published whole (shared_ptr, atomic load /
    // store) so the editor can poll "is this destination modulated?" from the message thread without the
    // modulation lock while a state restore rebuilds the list on another thread. Null before the first change.
    struct AssignedSnapshot { int revision = 0; std::vector<std::string> destinations; };
    std::shared_ptr<const AssignedSnapshot> assignedSnapshot() const noexcept { return std::atomic_load(&published); }



    // Evaluates all ControlSequences at songBeatPos, then applies every assignment
    // to paramValues. Values not present in paramValues are silently skipped.
    // paramValues is keyed by string_view (literals only). a.destinationId is
    // std::string which converts implicitly for lookup.
    void process(const std::vector<ControlSequence>& sequences,
                 double songBeatPos,
                 std::unordered_map<std::string_view, float>& paramValues) const;

private:
    std::vector<ModulationAssignment> assignments;
    int revision = 0;   // bumped by rebuildCache() on every add/remove (see assignmentsRevision)
    std::shared_ptr<const AssignedSnapshot> published;   // swapped by rebuildCache() with std::atomic_store

    // Pre-allocated work map reused each process() call to avoid heap allocation
    // on the audio thread.  process() is audio-thread-only, so this is safe.
    mutable std::unordered_map<std::string, float> workMap;

    // Scratch for the "<sequence id>_output" lookup key in process(): reserved in rebuildCache(), so
    // building the key never allocates on the audio thread, whatever the id's length.
    mutable std::string outputKeyScratch;

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
