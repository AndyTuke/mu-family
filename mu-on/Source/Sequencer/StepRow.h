#pragma once

#include "Persistence/TrackParams.h"   // EngineType (the default grooves)
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

// One 909 step row: 16 steps, each with an on/off and an accent flag, and the row's groove (swing, accent
// velocity). Cells are atomic so the message thread (grid edits) and the audio thread (the row's clock) share
// them without a lock or a dropped block. A row belongs to its Track, not to the track's engine, so it is kept
// when the engine changes (a Rumble track ignores its row). Rows serialise as <Track i on accent> nodes,
// carried inside each layer's saved node.
namespace mu_on
{

class StepRow
{
public:
    static constexpr int kNumSteps = 16;

    StepRow() { clear(); }

    bool isOn    (int step) const noexcept { return valid(step) && cells[(size_t) step].on.load(std::memory_order_relaxed); }
    bool isAccent(int step) const noexcept { return valid(step) && cells[(size_t) step].accent.load(std::memory_order_relaxed); }
    void setOn    (int step, bool v) noexcept { if (valid(step)) cells[(size_t) step].on.store(v, std::memory_order_relaxed); }
    void setAccent(int step, bool v) noexcept { if (valid(step)) cells[(size_t) step].accent.store(v, std::memory_order_relaxed); }

    void clear() noexcept
    {
        for (auto& c : cells) { c.on.store(false, std::memory_order_relaxed); c.accent.store(false, std::memory_order_relaxed); }
    }

    // The starting groove of an engine, so a new track plays something at once: four-on-the-floor kick
    // (downbeat accented), off-beat bass under it, straight 8th hats, backbeat snare, a sparse tom fill, nothing for Rumble.
    void loadDefaultGroove(EngineType engine) noexcept
    {
        clear();
        switch (engine)
        {
            case EngineType::Kick:
                for (int s = 0; s < kNumSteps; s += 4) setOn(s, true);
                setAccent(0, true);
                break;
            case EngineType::Bass:   for (int s = 2; s < kNumSteps; s += 4) setOn(s, true); break;
            case EngineType::Hat:    for (int s = 0; s < kNumSteps; s += 2) setOn(s, true); break;
            case EngineType::Snare:  setOn(4, true); setOn(12, true); break;
            case EngineType::Rumble: break;
            case EngineType::FmPerc: setOn(4, true); setOn(10, true); setOn(14, true); break;
        }
    }

    // The row's groove: its swing (0..0.9, the share of half a 16th the odd steps are pushed later) and the
    // velocity of its accented steps (0..1). The track sets them from its own t{N}_swing and t{N}_accent rows
    // at every block. Atomics: the message thread may seed, the audio thread reads.
    void setGroove(float swing01, float accentVelocity01) noexcept
    {
        swing.store(juce::jlimit(0.0f, 0.9f, swing01), std::memory_order_relaxed);
        accent.store(juce::jlimit(0.0f, 1.0f, accentVelocity01), std::memory_order_relaxed);
    }
    float swingAmount()    const noexcept { return swing.load(std::memory_order_relaxed); }
    float accentVelocity() const noexcept { return accent.load(std::memory_order_relaxed); }

    // The row as a <Track i on accent> node (also what a layer preset carries).
    juce::ValueTree serialise(int slot) const
    {
        juce::ValueTree row("Track");
        row.setProperty("i", slot, nullptr);
        juce::String on, ac;
        for (int s = 0; s < kNumSteps; ++s) { on += isOn(s) ? '1' : '0'; ac += isAccent(s) ? '1' : '0'; }
        row.setProperty("on", on, nullptr);
        row.setProperty("accent", ac, nullptr);
        return row;
    }

    // Load a <Track> row (its own "i" is ignored, so a row can move slots). Steps the row doesn't
    // cover, and an invalid node, clear the row.
    void deserialise(const juce::ValueTree& row)
    {
        const juce::String on = row.getProperty("on").toString();
        const juce::String ac = row.getProperty("accent").toString();
        for (int s = 0; s < kNumSteps; ++s)
        {
            setOn    (s, s < on.length() && on[s] == '1');
            setAccent(s, s < ac.length() && ac[s] == '1');
        }
    }

private:
    struct Cell { std::atomic<bool> on { false }; std::atomic<bool> accent { false }; };
    static bool valid(int s) noexcept { return s >= 0 && s < kNumSteps; }

    std::array<Cell, kNumSteps> cells;
    std::atomic<float> swing { 0.0f }, accent { 1.0f };   // the parameters' defaults
};

} // namespace mu_on
