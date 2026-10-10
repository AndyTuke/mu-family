#pragma once

#include "Control/ControlAction.h"
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// The saved table that maps a MIDI CC or note (on a channel) to a ControlAction, plus MIDI learn.
// One per processor, shared by every product, stored per user (not per preset) so a preset
// change never remaps the player's controller.
//
// Threads: the table is edited on the message thread. The audio thread only reads the lock-free
// lookup (`find`) and the per-mapping flags (`typeOf` / `quantiseOf`), which the edit methods
// republish; it never touches the strings. `latest` / `dirty` carry knob values the other way.
namespace mu_core
{

struct MidiMapping
{
    bool          isNote  = false;   // false = control change
    int           channel = 1;       // 1..16
    int           number  = 0;       // CC number or note number, 0..127
    ControlAction action;
};

class MidiControlMap
{
public:
    static constexpr int kMaxMappings = 512;

    MidiControlMap();

    // Persistence: set the file first, then load(); every edit saves.
    void setStorageFile(juce::File f);
    void load();
    void save() const;

    // Message-thread edits. add() replaces a mapping from the same source; returns its index (-1 if full).
    int     add(const MidiMapping& m);
    void    remove(int index);
    void    clear();
    int     size() const;
    MidiMapping get(int index) const;

    // App-wide quantise for actions whose own setting is Default.
    Quantise getQuantise() const noexcept { return (Quantise) globalQuantise.load(std::memory_order_relaxed); }
    void     setQuantise(Quantise q);

    // MIDI learn: arm, then the audio thread hands back the first CC / note it sees.
    void armLearn()    noexcept { learned.store(0, std::memory_order_relaxed); learning.store(true, std::memory_order_release); }
    void cancelLearn() noexcept { learning.store(false, std::memory_order_release); }
    bool isLearning() const noexcept { return learning.load(std::memory_order_acquire); }
    bool takeLearned(bool& isNote, int& channel, int& number) noexcept;

    // ---- audio thread (lock-free, no allocation) ----
    // The mapping index for a source, or -1.
    int find(bool isNote, int channel, int number) const noexcept
    {
        if (channel < 1 || channel > 16 || number < 0 || number > 127) return -1;
        return lookup[slot(isNote, channel, number)].load(std::memory_order_relaxed);
    }
    ControlActionType typeOf(int index)     const noexcept { return (ControlActionType) (flags[(size_t) index].load(std::memory_order_relaxed) & 0xFF); }
    Quantise          quantiseOf(int index) const noexcept { return (Quantise) ((flags[(size_t) index].load(std::memory_order_relaxed) >> 8) & 0xFF); }
    void offerLearn(bool isNote, int channel, int number) noexcept;

    // Bumped (odd while a rewrite is in progress, even when settled) by every edit, so the audio
    // thread can tell a settled table from one mid-rewrite, and queued indices can tell they went stale.
    uint32_t epoch() const noexcept { return epochCounter.load(std::memory_order_acquire); }

    // Latest knob value per mapping (audio -> message thread), coalescing a sweep to one write.
    // `dirty` holds 0 (nothing new) or the epoch it was written in plus one, so a flag set just
    // before an edit is recognised as stale and ignored rather than firing a different mapping.
    std::array<std::atomic<float>,    kMaxMappings> latest {};
    std::array<std::atomic<uint32_t>, kMaxMappings> dirty  {};

private:
    static size_t slot(bool isNote, int channel, int number) noexcept { return (size_t) (((isNote ? 1 : 0) * 16 + (channel - 1)) * 128 + number); }
    void republish();   // rebuild the audio-side lookup + flags from `mappings` (caller holds the lock)

    juce::File                    storageFile;
    mutable juce::CriticalSection lock;
    std::vector<MidiMapping>          mappings;

    std::array<std::atomic<int16_t>, 2 * 16 * 128> lookup {};
    std::array<std::atomic<uint32_t>, kMaxMappings> flags {};
    std::atomic<uint32_t> epochCounter { 0 };
    std::atomic<int>      globalQuantise { (int) Quantise::Bar };
    std::atomic<bool>     learning { false };
    std::atomic<uint32_t> learned  { 0 };   // 0 = nothing yet, else 1 | note<<1 | channel<<8 | number<<16
};

} // namespace mu_core
