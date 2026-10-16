#pragma once

#include "Sequencer/StepRow.h"
#include <array>

// The 909 grid as the processor and the tests address it: one row of 16 steps per layer slot (kMaxLayers
// rows). A row is owned by the Track that plays it (a StepRow member, with its own steps, swing and accent),
// so it travels with the track through add / delete / reorder; the pattern is only a view that addresses the
// tracks' rows by slot. `bind` points a slot at a track's row (the processor does it after every structural
// edit, under its layer lock); an unbound slot uses a row of the pattern's own, which is what a standalone
// pattern (the tests) works on. Not part of the layer library: a layer reads only its own StepRow.
namespace mu_on
{

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
