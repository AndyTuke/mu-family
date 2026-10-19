#include "VoiceSection.h"
#include "Sequencer/Rhythm.h"
#include "Persistence/ScopedApvtsLoading.h"
#include "Modulation/ModulationSnapshot.h"

namespace mu_clid {

VoiceSection::VoiceSection(ProcessorBase& base, RhythmHost& h)
    : host(h), pitchSub(h), filterSub(h), ampSub(h), insertSub(base, "r")
{
    // The FX sends are wired by AmpSubsection but sit in the Effects box (VoiceBand places them).
    const auto sends = ampSub.sendKnobs();
    setSections(pitchSub, filterSub, ampSub, insertSub, { sends[0], sends[1], sends[2] });

    // Forward status updates from each subsection through our own callback.
    auto fwd = [this](const juce::String& n, const juce::String& v) {
        if (onStatusUpdate) onStatusUpdate(n, v);
    };
    pitchSub .onStatusUpdate = fwd;
    filterSub.onStatusUpdate = fwd;
    ampSub   .onStatusUpdate = fwd;
    insertSub.onStatusUpdate = fwd;

    insertSub.onInsertAlgorithmChanged = [this](int charId) {
        if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
    };

    // mu-clid-specific insert-panel hooks (the shared subsection is product-agnostic).
    insertSub.isPlaying = [this] { return host.isRhythmTransportPlaying(); };
    insertSub.isSlotModulated = [this](int slot) -> bool
    {
        if (currentRhythm < 0 || currentRhythm >= host.getRhythmCount()) return false;
        const char* const dest[4] = { "insert.p1", "insert.p2", "insert.p3", "insert.p4" };
        const auto snap = host.getRhythmLayer(currentRhythm).modulationMatrix.assignedSnapshot();   // lock-free published copy
        if (snap == nullptr) return false;
        for (const auto& d : snap->destinations)
            if (d == dest[slot]) return true;
        return false;
    };
    insertSub.slotModValue = [this](int slot) -> float
    {
        const int snap[4] = { kSnapInsP1, kSnapInsP2, kSnapInsP3, kSnapInsP4 };
        return host.getRhythmModLive(currentRhythm, snap[slot]);
    };
    insertSub.getInsertGR = [this]() -> const std::atomic<float>*
    {
        return host.getRhythmInsertGainReduction(currentRhythm);
    };
    insertSub.runBulkChange = [this](std::function<void()> fn)
    {
        // Suppress the parameterChanged listener during the multi-write algo
        // switch, then resync the engine from APVTS (preset-load pattern).
        mu_core::ScopedApvtsLoading guard(host.getRhythmParamsLoadingFlag());
        fn();
        if (currentRhythm >= 0 && currentRhythm < host.getRhythmCount())
            host.syncRhythmFromParameters(currentRhythm);
    };
}

void VoiceSection::setRhythm(int ri)
{
    currentRhythm = ri;
    pitchSub .setRhythm(ri);
    filterSub.setRhythm(ri);
    ampSub   .setRhythm(ri);
    insertSub.setChannel(ri);
}

void VoiceSection::loadFromRhythm()
{
    pitchSub .loadFromRhythm();
    filterSub.loadFromRhythm();
    ampSub   .loadFromRhythm();
    insertSub.loadFromChannel();
}

void VoiceSection::refreshSuffix(const juce::String& suffix)
{
    pitchSub .refreshSuffix(suffix);
    filterSub.refreshSuffix(suffix);
    ampSub   .refreshSuffix(suffix);
    insertSub.refreshSuffix(suffix);
}

} // namespace mu_clid
