#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstddef>
#include <cstdint>

#include "MuLimits.h"
#include "Persistence/MidiFullPresetMap.h"
#include "Persistence/MidiPresetMap.h"

namespace mu_core
{
// MIDI program changes to preset loads. The audio thread scans each block's MIDI for program changes on channels 1-8
// (the per-slot map, gated by its channel mask) and channel 9 (the full-preset map, gated by isEnabled) and enqueues
// them in a lock-free FIFO; the message thread drains the FIFO and hands each event to the owner's loaders.
class ProgramChangeRouter
{
public:
    // Audio thread, allocation-free (raw bytes, not juce::MidiMessage). True when any program change was enqueued.
    bool scan(const juce::MidiBuffer& midi, const MidiPresetMap& presetMap, const MidiFullPresetMap& fullPresetMap)
    {
        const uint8_t chMask           = presetMap.getChannelMask();
        const bool    fullPresetEnabled = fullPresetMap.isEnabled();
        if (chMask == 0 && ! fullPresetEnabled) return false;

        bool needPC = false;
        for (const auto meta : midi)
        {
            // Raw bytes, not a juce::MidiMessage: constructing one allocates for messages over 8 bytes (SysEx).
            if (meta.numBytes < 2 || meta.numBytes > 3 || (meta.data[0] & 0xf0) != 0xc0) continue;
            const int ch = (meta.data[0] & 0x0f) + 1;      // 1-based, 1..16

            int  slot   = -1;
            bool isFull = false;
            if (ch >= 1 && ch <= 8)
            {
                if (! (chMask & (1 << (ch - 1)))) continue;
                slot = ch - 1;
            }
            else if (ch == MidiFullPresetMap::Channel)
            {
                if (! fullPresetEnabled) continue;
                isFull = true;
            }
            else
            {
                continue;
            }

            int start1, size1, start2, size2;
            fifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 + size2 > 0)
            {
                const int dst = (size1 > 0) ? start1 : start2;
                queue[(size_t) dst] = { slot, meta.data[1] & 0x7f, isFull };
                fifo.finishedWrite(1);
                needPC = true;
            }
        }
        return needPC;
    }

    // Message thread: drains the FIFO. A full-preset event with a stored file calls applyFull(file); a slot event for
    // a live layer (slot < activeLayers) with a stored file calls applySlot(slot, file).
    template <typename ApplyFull, typename ApplySlot>
    void drain(int activeLayers, const MidiPresetMap& presetMap, const MidiFullPresetMap& fullPresetMap,
               ApplyFull&& applyFull, ApplySlot&& applySlot)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;

        int start1, size1, start2, size2;
        fifo.prepareToRead(ready, start1, size1, start2, size2);

        auto handle = [&](const Event& ev)
        {
            if (ev.fullPreset)
            {
                if (! fullPresetMap.hasPreset(ev.presetIndex)) return;
                const juce::File f { fullPresetMap.getPresetPath(ev.presetIndex) };
                if (f.existsAsFile())
                    applyFull(f);
                return;
            }
            if (ev.slot < 0 || ev.slot >= activeLayers) return;
            if (! presetMap.hasPreset(ev.presetIndex))   return;
            const juce::File f { presetMap.getPresetPath(ev.presetIndex) };
            if (f.existsAsFile())
                applySlot(ev.slot, f);
        };
        for (int i = 0; i < size1; ++i) handle(queue[(size_t)(start1 + i)]);
        for (int i = 0; i < size2; ++i) handle(queue[(size_t)(start2 + i)]);
        fifo.finishedRead(ready);
    }

private:
    struct Event { int slot; int presetIndex; bool fullPreset; };
    static constexpr int kFifoSize = mu_limits::kProgramChangeFifoSize;
    juce::AbstractFifo           fifo { kFifoSize };
    std::array<Event, kFifoSize> queue {};
};

} // namespace mu_core
