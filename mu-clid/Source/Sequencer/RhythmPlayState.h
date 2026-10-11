#pragma once

#include <atomic>

namespace mu_clid {

// One rhythm's live play state: written by the audio thread, read by the UI at 30 Hz. Its own
// header so the rhythm circle can show it without knowing which processor owns it.
struct RhythmPlayState
{
    std::atomic<int>  currentStep   { 0 };
    std::atomic<int>  currentStepC  { 0 }; // effectiveStep % stepsC - independent of combined-pattern length
    std::atomic<int>  patternLength { 1 };
    std::atomic<int>  stepsA        { 1 }; // individual ring step counts for per-ring rotation
    std::atomic<int>  stepsB        { 1 };
    std::atomic<int>  stepsC        { 1 };
    std::atomic<int>  hitCount      { 0 }; // monotonic counter - audio thread increments per hit, UI tracks lastSeen (avoids one-shot-flag race between RhythmCircle + SidebarItem)
};

} // namespace mu_clid
