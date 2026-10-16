#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <cstring>

// Hands parameter ids from a thread that must not allocate (the audio thread, when a host plays back
// automation and APVTS calls the editor's listener there) to the message thread, which drains them on its
// timer and refreshes the controls. post() copies the id into a fixed slot: no heap allocation, no message-queue
// post, no lock the message thread can hold (the producer lock is only ever try-locked). The editor's timer
// calls drain().
//
// If the queue is full, or another off-thread caller holds the producer lock at that instant, the id is
// dropped: the control then shows the old value until its next change. That costs one stale refresh in a
// very unlikely case, against a dropout risk on every automated block.
namespace mu_ui
{
class ParamRefreshQueue
{
public:
    // Any thread, real-time safe. Ids longer than kIdChars - 1 bytes are not queued.
    void post (const juce::String& id) noexcept
    {
        const juce::SpinLock::ScopedTryLockType lock (producerLock);
        if (! lock.isLocked())
            return;

        const char* utf8 = id.toRawUTF8();
        const size_t n   = std::strlen (utf8);
        if (n == 0 || n >= (size_t) kIdChars)
            return;

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 < 1)
            return;

        std::memcpy (slots[(size_t) start1].chars.data(), utf8, n + 1);
        fifo.finishedWrite (1);
    }

    // Message thread only: calls fn (const juce::String& id) for every queued id, oldest first.
    template <class Fn>
    void drain (Fn&& fn)
    {
        for (;;)
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            fifo.prepareToRead (1, start1, size1, start2, size2);
            if (size1 < 1)
                return;

            const juce::String id = juce::String::fromUTF8 (slots[(size_t) start1].chars.data());
            fifo.finishedRead (1);
            fn (id);
        }
    }

private:
    static constexpr int kSlots   = 256;
    static constexpr int kIdChars = 48;

    struct Slot { std::array<char, kIdChars> chars {}; };

    juce::AbstractFifo         fifo { kSlots };
    std::array<Slot, kSlots>   slots {};
    juce::SpinLock             producerLock;
};
} // namespace mu_ui
