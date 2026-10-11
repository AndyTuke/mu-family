#include "Control/MidiControlRouter.h"
#include <cmath>

namespace mu_core
{

MidiControlRouter::MidiControlRouter(MidiControlMap& mapToUse) : map(mapToUse)
{
    scratch.ensureSize(kScratchBytes);   // message thread: the audio thread never grows it
}

void MidiControlRouter::noteBlock(const BlockTransport& transport, double beatsPerBar) noexcept
{
    // The beat should continue where the last block ended; anything else is a loop, relocate or jump,
    // and a waiting action's boundary no longer means what it did.
    const double expected = last.startBeat + last.blockBeats;
    discontinuity = transport.playing && last.playing
                 && std::abs(transport.startBeat - expected) > 0.05 + 0.5 * last.blockBeats;

    last     = transport;
    barBeats = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    playOutside.store(transport.playOutside, std::memory_order_relaxed);
}

bool MidiControlRouter::push(int mapping, float value, uint32_t epoch) noexcept
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 == 0) return false;   // full: drop (a held-down pad can't flood it)
    queue[(size_t) (size1 > 0 ? start1 : start2)] = { (int16_t) mapping, value, epoch };
    fifo.finishedWrite(1);
    return true;
}

// Release the quantised actions whose boundary falls in the block about to be resolved (the
// same test queueAction uses), or that must not keep waiting: playback stopped, the beat jumped,
// or the table was edited since they were queued (their index may now mean another mapping).
bool MidiControlRouter::releaseDue(uint32_t epoch) noexcept
{
    bool any = false;
    // The block about to be resolved ends here, plus the poll interval: a released action still
    // waits for the message thread's next tick, so it is handed over early enough to land on the beat.
    const double upcomingEnd = last.startBeat + 2.0 * last.blockBeats + pollLeadBeats();
    for (int i = 0; i < numPending;)
    {
        const auto p = pending[(size_t) i];
        const bool stale = p.epoch != epoch;
        const bool due   = ! last.playing || discontinuity || p.boundaryBeat <= upcomingEnd;
        if (stale || due)
        {
            if (! stale) any |= push(p.mapping, p.value, p.epoch);
            pending[(size_t) i] = pending[(size_t) --numPending];
        }
        else
            ++i;
    }
    return any;
}

// An action that changes what is heard: fire at once, or wait for the next beat / bar.
bool MidiControlRouter::queueAction(int mapping, float value, uint32_t epoch) noexcept
{
    const auto type = map.typeOf(mapping);
    if (! isQuantisable(type))
        return push(mapping, value, epoch);

    auto q = map.quantiseOf(mapping);
    if (q == Quantise::Default) q = map.getQuantise();
    if (q == Quantise::Off || ! last.playing)
        return push(mapping, value, epoch);

    // The press lands in the block about to be resolved, which starts where the last one ended.
    const double step     = q == Quantise::Beat ? 1.0 : barBeats;
    const double now      = last.startBeat + last.blockBeats;
    const double boundary = std::ceil(now / step - 1.0e-9) * step;
    if (boundary - now <= last.blockBeats + pollLeadBeats() || numPending >= kMaxPending)
        return push(mapping, value, epoch);

    pending[(size_t) numPending++] = { (int16_t) mapping, value, epoch, boundary };
    return false;
}

bool MidiControlRouter::process(juce::MidiBuffer& midi) noexcept
{
    // A table mid-rewrite (odd epoch) is not read: this block passes through untouched.
    const uint32_t epoch = map.epoch();
    const bool settled   = (epoch & 1u) == 0;
    bool wake = settled && releaseDue(epoch);
    if (! settled) return wake;

    const bool learning = map.isLearning();

    // With nothing mapped and no learn in progress there is nothing to claim: leave the block be.
    if (! learning && map.isEmptyRealtime()) return wake;

    // The CC / note a message carries, if it is one the map or MIDI learn may claim. Read straight
    // from the raw bytes: building a juce::MidiMessage allocates for anything over 8 bytes (SysEx).
    struct Source { bool valid = false, isNote = false; int channel = 0, number = 0; float value = 0.0f; bool press = false; };
    auto sourceOf = [](const juce::MidiMessageMetadata& meta) noexcept
    {
        Source s;
        if (meta.numBytes != 3) return s;   // CC and note messages are three bytes; anything else passes through
        const int status  = meta.data[0];
        const int channel = (status & 0x0f) + 1;
        const int number  = meta.data[1] & 0x7f;
        const int value   = meta.data[2] & 0x7f;
        switch (status & 0xf0)
        {
            case 0xb0: s = { true, false, channel, number, (float) value / 127.0f, value > 0 }; break;
            case 0x90: s = value > 0 ? Source { true, true, channel, number, 1.0f, true }
                                     : Source { true, true, channel, number, 0.0f, false }; break;   // velocity 0 = note off
            case 0x80: s = { true, true, channel, number, 0.0f, false }; break;
            default: break;
        }
        return s;
    };

    // Whether the rebuilt block fits the reserved scratch (else claimed messages are acted on but left in).
    int bytes = 0;
    for (const auto meta : midi) bytes += meta.numBytes + 8;
    const bool canStrip = bytes <= kScratchBytes;

    // One pass: act on every message a mapping (or learn) claims; copy the rest to scratch. The
    // claim decision is made once per message, so what is acted on is exactly what is removed.
    int claimed = 0;
    if (canStrip) scratch.clear();
    for (const auto meta : midi)
    {
        const auto s = sourceOf(meta);
        bool taken = false;

        if (s.valid && learning)
        {
            // Learn takes the first press (a release or a CC of 0 passes through, so a held note still ends).
            if (s.press) { map.offerLearn(s.isNote, s.channel, s.number); taken = true; }
        }
        else if (s.valid)
        {
            const int idx = map.find(s.isNote, s.channel, s.number);
            // The table must not have been edited between the lookup and the flag read.
            std::atomic_thread_fence(std::memory_order_acquire);   // the lookup read must finish before the epoch re-check
            if (idx >= 0 && map.epoch() == epoch)
            {
                taken = true;
                if (map.typeOf(idx) == ControlActionType::Parameter)
                {
                    map.latest[(size_t) idx].store(s.value, std::memory_order_relaxed);
                    map.dirty[(size_t) idx].store(epoch + 1, std::memory_order_release);
                    wake = true;
                }
                else if (s.press)   // a button acts on its press, not its release
                    wake |= queueAction(idx, s.value, epoch);
            }
        }

        if (taken) ++claimed;
        else if (canStrip) scratch.addEvent(meta.data, meta.numBytes, meta.samplePosition);
    }

    // Replace the block with what is left. `midi` held a superset of these bytes, so refilling
    // it never grows it; `scratch` is never swapped, so it keeps its reserve.
    if (canStrip && claimed > 0)
    {
        midi.clear();
        midi.addEvents(scratch, 0, -1, 0);
    }
    return wake;
}

bool MidiControlRouter::drain(ControlSink& sink)
{
    const uint32_t epoch = map.epoch();
    if ((epoch & 1u) != 0) return true;   // mid-edit: leave it queued; the caller asks for another drain

    // Knobs: the latest value of each mapping that moved since the last drain.
    for (int i = 0, n = map.size(); i < n; ++i)
        if (map.dirty[(size_t) i].exchange(0, std::memory_order_acquire) == epoch + 1)
        {
            const auto m = map.get(i);
            if (m.action.type != ControlActionType::Parameter) continue;
            auto a  = m.action;
            a.value = map.latest[(size_t) i].load(std::memory_order_relaxed);
            sink.perform(a);
        }

    // Buttons: the queued actions, oldest first; any queued before an edit are dropped.
    const int ready = fifo.getNumReady();
    if (ready <= 0) return false;
    int start1, size1, start2, size2;
    fifo.prepareToRead(ready, start1, size1, start2, size2);
    auto handle = [&](const Event& e)
    {
        if (e.epoch != epoch) return;
        const auto m = map.get(e.mapping);
        if (m.action.type == ControlActionType::None) return;
        auto a  = m.action;
        a.value = e.value;
        sink.perform(a);
    };
    for (int i = 0; i < size1; ++i) handle(queue[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) handle(queue[(size_t) (start2 + i)]);
    fifo.finishedRead(ready);
    return false;
}

} // namespace mu_core
