#pragma once

#include "Persistence/TrackParams.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

// The 909 grid: one row of 16 steps per layer slot (kMaxLayers rows). Each cell carries an on/off and
// an accent flag. Cells are atomic so the message thread (grid edits) and the audio thread (sequencer
// read) share them without a lock or a dropped block. The row belongs to the track, not to its engine,
// so it is kept when the engine changes (a Rumble track ignores its row).
//
// A row is owned by the Track that plays it (a StepRow member), so it travels with the track through
// add / delete / reorder; the pattern only addresses the rows by slot. `bind` points a slot at a
// track's row (the processor does it after every structural edit, under its layer lock); an unbound
// slot uses a row of the pattern's own, which is what a standalone pattern (the tests) works on.
// Rows serialise as <Track i on accent> nodes, carried inside each layer's saved node.
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
    // (downbeat accented), off-beat bass under it, straight 8th hats, backbeat snare, nothing for Rumble.
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
        }
    }

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
};

class StepPattern
{
public:
    static constexpr int kNumTracks = 8;                    // one row per layer slot (mu_limits::kMaxLayers)
    static constexpr int kNumSteps  = StepRow::kNumSteps;

    StepPattern() { for (int t = 0; t < kNumTracks; ++t) bound[(size_t) t] = &owned[(size_t) t]; }

    // Point slot `slot` at `row` (a track's own row); null returns it to the pattern's own row.
    // Message thread, with the processor's layer lock held when the audio thread may be reading.
    void bind(int slot, StepRow* row) noexcept
    {
        if (slot >= 0 && slot < kNumTracks) bound[(size_t) slot] = row != nullptr ? row : &owned[(size_t) slot];
    }

    bool isOn    (int track, int step) const noexcept { return valid(track) && bound[(size_t) track]->isOn(step); }
    bool isAccent(int track, int step) const noexcept { return valid(track) && bound[(size_t) track]->isAccent(step); }

    void setOn    (int track, int step, bool v) noexcept { if (valid(track)) bound[(size_t) track]->setOn(step, v); }
    void setAccent(int track, int step, bool v) noexcept { if (valid(track)) bound[(size_t) track]->setAccent(step, v); }
    void toggle   (int track, int step) noexcept { if (valid(track)) setOn(track, step, ! isOn(track, step)); }

    void clear() noexcept { for (auto* r : bound) r->clear(); }

    // Row `track` at the starting groove of `engine`.
    void loadDefaultGroove(int track, EngineType engine) noexcept { if (valid(track)) bound[(size_t) track]->loadDefaultGroove(engine); }

    // ── Persistence ──────────────────────────────────────────────────────────
    // Replaces any existing <Pattern> child on `parent` with the current rows.
    void serialise(juce::ValueTree parent) const
    {
        parent.removeChild(parent.getChildWithName("Pattern"), nullptr);
        juce::ValueTree pat("Pattern");
        pat.setProperty("tracks", kNumTracks, nullptr);
        pat.setProperty("steps",  kNumSteps,  nullptr);
        for (int t = 0; t < kNumTracks; ++t)
            pat.addChild(serialiseTrack(t), -1, nullptr);
        parent.addChild(pat, -1, nullptr);
    }

    // One slot's row as a <Track i on accent> node (also what a layer preset carries).
    juce::ValueTree serialiseTrack(int t) const
    {
        return valid(t) ? bound[(size_t) t]->serialise(t) : juce::ValueTree();
    }

    // Load a <Track> row into slot `t`. Steps the row doesn't cover are cleared.
    void deserialiseTrack(int t, const juce::ValueTree& row)
    {
        if (valid(t)) bound[(size_t) t]->deserialise(row);
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
            deserialiseTrack((int) row.getProperty("i", -1), row);
        }
    }

private:
    static bool valid(int t) noexcept { return t >= 0 && t < kNumTracks; }

    std::array<StepRow, kNumTracks>  owned;
    std::array<StepRow*, kNumTracks> bound {};
};

} // namespace mu_on
