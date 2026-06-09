#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

// StepPattern — the 909 grid: 4 instrument tracks (Kick/Bass/Hat/Snare) × 16 steps.
// Each cell carries an on/off and an accent flag. Cells are atomic so the message
// thread (grid edits) and the audio thread (sequencer read) share them without a lock
// or a dropped block. Serialises into a <Pattern> child of the APVTS state tree
// (mirrors mu-tant's gate-pattern persistence — NOT 64 automatable APVTS params).
namespace mu_on
{

class StepPattern
{
public:
    static constexpr int kNumTracks = 4;
    static constexpr int kNumSteps  = 16;

    StepPattern() { clear(); }

    bool isOn    (int track, int step) const noexcept { return valid(track, step) && cell(track, step).on.load(std::memory_order_relaxed); }
    bool isAccent(int track, int step) const noexcept { return valid(track, step) && cell(track, step).accent.load(std::memory_order_relaxed); }

    void setOn    (int track, int step, bool v) noexcept { if (valid(track, step)) cell(track, step).on.store(v, std::memory_order_relaxed); }
    void setAccent(int track, int step, bool v) noexcept { if (valid(track, step)) cell(track, step).accent.store(v, std::memory_order_relaxed); }
    void toggle   (int track, int step) noexcept { if (valid(track, step)) setOn(track, step, ! isOn(track, step)); }

    void clear() noexcept
    {
        for (auto& t : cells)
            for (auto& c : t) { c.on.store(false, std::memory_order_relaxed); c.accent.store(false, std::memory_order_relaxed); }
    }

    // A simple default groove so a fresh instance grooves out of the box: four-on-the-floor
    // kick, backbeat snare, straight 8th hats, off-beat bass under the kick.
    void loadDefaultGroove() noexcept
    {
        clear();
        for (int s = 0; s < kNumSteps; s += 4) setOn(0, s, true);                 // Kick: 1,5,9,13
        setOn(3, 4, true); setOn(3, 12, true);                                    // Snare: backbeats
        for (int s = 0; s < kNumSteps; s += 2) setOn(2, s, true);                 // Hat: every 8th
        for (int s = 2; s < kNumSteps; s += 4) setOn(1, s, true);                 // Bass: off the kick
        setAccent(0, 0, true);                                                    // downbeat accent
    }

    // ── Persistence ──────────────────────────────────────────────────────────
    // Replaces any existing <Pattern> child on `parent` with the current grid.
    void serialise(juce::ValueTree parent) const
    {
        parent.removeChild(parent.getChildWithName("Pattern"), nullptr);
        juce::ValueTree pat("Pattern");
        pat.setProperty("tracks", kNumTracks, nullptr);
        pat.setProperty("steps",  kNumSteps,  nullptr);
        for (int t = 0; t < kNumTracks; ++t)
        {
            juce::ValueTree row("Track");
            row.setProperty("i", t, nullptr);
            juce::String on, ac;
            for (int s = 0; s < kNumSteps; ++s) { on += isOn(t, s) ? '1' : '0'; ac += isAccent(t, s) ? '1' : '0'; }
            row.setProperty("on", on, nullptr);
            row.setProperty("accent", ac, nullptr);
            pat.addChild(row, -1, nullptr);
        }
        parent.addChild(pat, -1, nullptr);
    }

    void deserialise(const juce::ValueTree& parent)
    {
        // Clear first so a state with no <Pattern> child (old/hand-edited preset) loads a
        // clean grid instead of silently inheriting the previously-loaded steps.
        clear();
        const auto pat = parent.getChildWithName("Pattern");
        if (! pat.isValid()) return;
        for (int i = 0; i < pat.getNumChildren(); ++i)
        {
            const auto row = pat.getChild(i);
            const int t = (int) row.getProperty("i", -1);
            if (t < 0 || t >= kNumTracks) continue;
            const juce::String on = row.getProperty("on").toString();
            const juce::String ac = row.getProperty("accent").toString();
            for (int s = 0; s < kNumSteps && s < on.length(); ++s) setOn(t, s, on[s] == '1');
            for (int s = 0; s < kNumSteps && s < ac.length(); ++s) setAccent(t, s, ac[s] == '1');
        }
    }

private:
    struct Cell { std::atomic<bool> on { false }; std::atomic<bool> accent { false }; };

    static bool valid(int t, int s) noexcept { return t >= 0 && t < kNumTracks && s >= 0 && s < kNumSteps; }
    Cell&       cell(int t, int s)       noexcept { return cells[(size_t) t][(size_t) s]; }
    const Cell& cell(int t, int s) const noexcept { return cells[(size_t) t][(size_t) s]; }

    std::array<std::array<Cell, kNumSteps>, kNumTracks> cells;
};

} // namespace mu_on
