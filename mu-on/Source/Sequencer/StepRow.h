#pragma once

#include "Persistence/TrackParams.h"   // EngineType (the default grooves)
#include "Audio/ChordVoicing.h"        // kDefaultChord, the chord limits
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
    void setOn    (int step, bool v) noexcept
    {
        if (! valid(step)) return;
        cells[(size_t) step].on.store(v, std::memory_order_relaxed);
        if (! v) { setTied(step, false); setTied(step + 1, false); }   // a step that goes off ends any tie into or out of it
    }
    // Tie: this step continues the note of the step before it (a Bass slides into it instead of retriggering).
    // A tie only counts between two steps that are both on.
    bool isTied  (int step) const noexcept { return valid(step) && cells[(size_t) step].tie.load(std::memory_order_relaxed); }
    void setTied (int step, bool v) noexcept { if (valid(step)) cells[(size_t) step].tie.store(v, std::memory_order_relaxed); }
    bool tiedFrom(int step) const noexcept { return step > 0 && isTied(step) && isOn(step) && isOn(step - 1); }
    void setAccent(int step, bool v) noexcept
    {
        if (! valid(step)) return;
        cells[(size_t) step].accent.store(v, std::memory_order_relaxed);
        if (v) cells[(size_t) step].ghost.store(false, std::memory_order_relaxed);   // accent and ghost exclude each other
    }
    // Ghost: a drum or percussion step played softly, at the row's Ghost level (a share of the normal note). Excludes accent.
    bool isGhost (int step) const noexcept { return valid(step) && cells[(size_t) step].ghost.load(std::memory_order_relaxed); }
    void setGhost(int step, bool v) noexcept
    {
        if (! valid(step)) return;
        cells[(size_t) step].ghost.store(v, std::memory_order_relaxed);
        if (v) cells[(size_t) step].accent.store(false, std::memory_order_relaxed);
    }
    // Open: a Hat step that plays the open hat (the other tracks ignore it). With on and accent it makes the four Hat
    // states Closed / Closed Accent / Open / Open Accent.
    bool isOpen  (int step) const noexcept { return valid(step) && cells[(size_t) step].open.load(std::memory_order_relaxed); }
    void setOpen  (int step, bool v) noexcept { if (valid(step)) cells[(size_t) step].open.store(v, std::memory_order_relaxed); }
    // Per step: the chance it plays (0..100 %, 100 default; the layer's own probability multiplies it) and the loop
    // ratio, an index into kLoopRatios (0 = 1:1, every pass; 1:2 plays the first of every two passes of the row).
    int  stepProb(int step) const noexcept { return valid(step) ? cells[(size_t) step].prob.load(std::memory_order_relaxed) : 100; }
    int  stepLoop(int step) const noexcept { return valid(step) ? cells[(size_t) step].loop.load(std::memory_order_relaxed) : 0; }
    void setStepProb(int step, int percent) noexcept { if (valid(step)) cells[(size_t) step].prob.store(juce::jlimit(0, 100, percent), std::memory_order_relaxed); }
    void setStepLoop(int step, int ratioIndex) noexcept
    {
        if (valid(step)) cells[(size_t) step].loop.store(juce::jlimit(0, (int) std::size(kLoopRatios) - 1, ratioIndex), std::memory_order_relaxed);
    }

    // Per step pitch (a Kick or Bass): the octave (-2 to +2) and the note offset (-12 to +12 semitones) from the Tune.
    int  stepOctave(int step) const noexcept { return valid(step) ? cells[(size_t) step].oct.load(std::memory_order_relaxed) : 0; }
    int  stepNote  (int step) const noexcept { return valid(step) ? cells[(size_t) step].note.load(std::memory_order_relaxed) : 0; }
    void setStepOctave(int step, int v) noexcept { if (valid(step)) cells[(size_t) step].oct.store(juce::jlimit(-2, 2, v), std::memory_order_relaxed); }
    void setStepNote  (int step, int v) noexcept { if (valid(step)) cells[(size_t) step].note.store(juce::jlimit(-12, 12, v), std::memory_order_relaxed); }
    int  stepSemitones(int step) const noexcept { return stepOctave(step) * 12 + stepNote(step); }
    // Set the octave and note so that the step sounds `semitones` from its tune (octave kept when the note fits, -36 to +36).
    void setStepSemitones(int step, int semitones) noexcept
    {
        semitones = juce::jlimit(-36, 36, semitones);
        int oct = stepOctave(step);
        int note = semitones - 12 * oct;
        if (std::abs(note) > 12)
        {
            oct = juce::jlimit(-2, 2, juce::roundToInt(semitones / 12.0f));
            note = semitones - 12 * oct;
        }
        setStepOctave(step, oct);
        setStepNote(step, note);
    }

    // Per step chord (a Chord track): the chord (an index into kChords), its inversion and its spread. The root is the
    // step's Note (a degree of the master scale: 0 is the key note) and its Octave.
    int  stepChord    (int step) const noexcept { return valid(step) ? cells[(size_t) step].chord.load(std::memory_order_relaxed) : kDefaultChord; }
    int  stepInversion(int step) const noexcept { return valid(step) ? cells[(size_t) step].inv.load(std::memory_order_relaxed) : 0; }
    int  stepSpread   (int step) const noexcept { return valid(step) ? cells[(size_t) step].spread.load(std::memory_order_relaxed) : 0; }
    void setStepChord    (int step, int v) noexcept { if (valid(step)) cells[(size_t) step].chord.store(juce::jlimit(0, kNumChords - 1, v), std::memory_order_relaxed); }
    void setStepInversion(int step, int v) noexcept { if (valid(step)) cells[(size_t) step].inv.store(juce::jlimit(0, kMaxChordInversion, v), std::memory_order_relaxed); }
    void setStepSpread   (int step, int v) noexcept { if (valid(step)) cells[(size_t) step].spread.store(juce::jlimit(0, kMaxChordSpread, v), std::memory_order_relaxed); }
    ChordHit stepChordHit(int step) const noexcept { return { stepChord(step), stepInversion(step), stepSpread(step), stepNote(step), stepOctave(step) }; }

    void clear() noexcept
    {
        for (auto& c : cells) { c.on.store(false, std::memory_order_relaxed); c.accent.store(false, std::memory_order_relaxed); c.ghost.store(false, std::memory_order_relaxed); c.open.store(false, std::memory_order_relaxed); c.prob.store(100, std::memory_order_relaxed); c.loop.store(0, std::memory_order_relaxed); c.tie.store(false, std::memory_order_relaxed); c.oct.store(0, std::memory_order_relaxed); c.note.store(0, std::memory_order_relaxed);
                    c.chord.store(kDefaultChord, std::memory_order_relaxed); c.inv.store(0, std::memory_order_relaxed); c.spread.store(0, std::memory_order_relaxed); }
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
            case EngineType::Chord:  for (int s = 2; s < kNumSteps; s += 4) setOn(s, true); break;   // off-beat stabs
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
    // The Ghost level: the velocity of a ghost step as a share (0..1) of a normal note's. The track sets it from t{N}_ghost.
    void  setGhostLevel(float level01) noexcept { ghostShare.store(juce::jlimit(0.0f, 1.0f, level01), std::memory_order_relaxed); }
    float ghostLevel() const noexcept { return ghostShare.load(std::memory_order_relaxed); }

    // The row as a <Track i on accent> node (also what a layer preset carries).
    juce::ValueTree serialise(int slot) const
    {
        juce::ValueTree row("Track");
        row.setProperty("i", slot, nullptr);
        juce::String on, ac, op, tie, gh;
        for (int s = 0; s < kNumSteps; ++s) { on += isOn(s) ? '1' : '0'; ac += isAccent(s) ? '1' : '0'; op += isOpen(s) ? '1' : '0'; tie += tiedFrom(s) ? '1' : '0'; gh += isGhost(s) ? '1' : '0'; }
        row.setProperty("on", on, nullptr);
        row.setProperty("accent", ac, nullptr);
        if (gh.containsChar('1')) row.setProperty("ghost", gh, nullptr);  // absent = no ghost steps
        if (op.containsChar('1')) row.setProperty("open", op, nullptr);   // absent = every step closed
        if (tie.containsChar('1')) row.setProperty("tie", tie, nullptr);  // absent = no tied steps
        // Probability as a comma list and the loop ratio as one digit per step, each only when a step leaves its default.
        juce::StringArray probs; juce::String loops; bool anyProb = false, anyLoop = false;
        for (int s = 0; s < kNumSteps; ++s)
        {
            probs.add(juce::String(stepProb(s))); loops += juce::String(stepLoop(s));
            anyProb = anyProb || stepProb(s) != 100; anyLoop = anyLoop || stepLoop(s) != 0;
        }
        juce::StringArray octs, notes; bool anyPitch = false;
        for (int s = 0; s < kNumSteps; ++s) { octs.add(juce::String(stepOctave(s))); notes.add(juce::String(stepNote(s))); anyPitch = anyPitch || stepSemitones(s) != 0; }
        juce::StringArray chords, invs, spreads; bool anyChord = false;
        for (int s = 0; s < kNumSteps; ++s)
        {
            chords.add(juce::String(stepChord(s))); invs.add(juce::String(stepInversion(s))); spreads.add(juce::String(stepSpread(s)));
            anyChord = anyChord || stepChord(s) != kDefaultChord || stepInversion(s) != 0 || stepSpread(s) != 0;
        }
        if (anyChord) { row.setProperty("chord", chords.joinIntoString(","), nullptr); row.setProperty("inv", invs.joinIntoString(","), nullptr); row.setProperty("spread", spreads.joinIntoString(","), nullptr); }
        if (anyPitch) { row.setProperty("oct", octs.joinIntoString(","), nullptr); row.setProperty("note", notes.joinIntoString(","), nullptr); }
        if (anyProb) row.setProperty("prob", probs.joinIntoString(","), nullptr);
        if (anyLoop) row.setProperty("loop", loops, nullptr);
        return row;
    }

    // Load a <Track> row (its own "i" is ignored, so a row can move slots). Steps the row doesn't
    // cover, and an invalid node, clear the row.
    void deserialise(const juce::ValueTree& row)
    {
        const juce::String on = row.getProperty("on").toString();
        const juce::String ac = row.getProperty("accent").toString();
        const juce::String op = row.getProperty("open").toString();
        const juce::String tie = row.getProperty("tie").toString();
        const juce::String gh = row.getProperty("ghost").toString();
        const auto octs = juce::StringArray::fromTokens(row.getProperty("oct").toString(), ",", "");
        const auto notes = juce::StringArray::fromTokens(row.getProperty("note").toString(), ",", "");
        const auto chordList = juce::StringArray::fromTokens(row.getProperty("chord").toString(), ",", "");
        const auto invList = juce::StringArray::fromTokens(row.getProperty("inv").toString(), ",", "");
        const auto spreadList = juce::StringArray::fromTokens(row.getProperty("spread").toString(), ",", "");
        const auto probs = juce::StringArray::fromTokens(row.getProperty("prob").toString(), ",", "");
        const juce::String loops = row.getProperty("loop").toString();
        for (int s = 0; s < kNumSteps; ++s)
        {
            setOpen  (s, s < op.length() && op[s] == '1');
            setStepProb(s, s < probs.size() ? probs[s].getIntValue() : 100);
            setStepLoop(s, s < loops.length() && juce::CharacterFunctions::isDigit(loops[s]) ? (int) (loops[s] - '0') : 0);
            setOn    (s, s < on.length() && on[s] == '1');
            setAccent(s, s < ac.length() && ac[s] == '1');
            setGhost (s, s < gh.length() && gh[s] == '1' && ! isAccent(s));
            setTied  (s, s < tie.length() && tie[s] == '1');
            setStepOctave(s, s < octs.size() ? octs[s].getIntValue() : 0);
            setStepNote  (s, s < notes.size() ? notes[s].getIntValue() : 0);
            setStepChord    (s, s < chordList.size() ? chordList[s].getIntValue() : kDefaultChord);
            setStepInversion(s, s < invList.size() ? invList[s].getIntValue() : 0);
            setStepSpread   (s, s < spreadList.size() ? spreadList[s].getIntValue() : 0);
        }
    }

private:
    struct Cell { std::atomic<bool> on { false }; std::atomic<bool> accent { false }; std::atomic<bool> ghost { false }; std::atomic<bool> open { false }; std::atomic<int> prob { 100 }; std::atomic<int> loop { 0 }; std::atomic<bool> tie { false }; std::atomic<int> oct { 0 }; std::atomic<int> note { 0 };
                  std::atomic<int> chord { kDefaultChord }; std::atomic<int> inv { 0 }; std::atomic<int> spread { 0 }; };
    static bool valid(int s) noexcept { return s >= 0 && s < kNumSteps; }

    std::array<Cell, kNumSteps> cells;
    std::atomic<float> swing { 0.0f }, accent { 1.0f }, ghostShare { 0.35f };   // the parameters' defaults
};

} // namespace mu_on
