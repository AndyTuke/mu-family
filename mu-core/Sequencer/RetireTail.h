#pragma once

#include "Sequencer/RenderContext.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

// What a retiring layer (or a product's engine tail) is to the shared RetireBin, and the pieces a layer
// type uses to keep its tail honest. docs/mu-tronic/design.md 2.1.3 (C2) and 2.1.4 (C3).
//
// A tail reads only its own DSP state, its declared snapshot, the transport fields of the context it is
// given and a service through its live layer's discipline. It never reads a parameter (bound or by id),
// a modulation matrix, a control sequence, a peer, the MIDI input, another layer or the processor.
namespace mu_core
{
    // How a layer leaves: None is dropped after the grace period (not parked at all), Fade is a linear
    // ramp to zero over fadeMs, Natural rings until renderRetiring reports silence. Every kind is bounded
    // by maxSeconds (clamped to mu_limits::kRetireHardCapSeconds), the lifetime of a tail that never goes
    // silent (reverb, comb and resonator tails). Lengths are seconds, converted at park time with the
    // prepared rate, so they do not depend on the host block size.
    struct RetirePolicy
    {
        enum class Kind : uint8_t { None, Fade, Natural };
        Kind  kind       = Kind::None;
        float fadeMs     = 25.0f;
        float maxSeconds = 2.0f;
        // Natural only: renderRetiring's "silent" is a drain criterion, not a level (a feedback insert may still ring),
        // so the bin closes the tail with its 10 ms declick instead of cutting it there (mu-Clid's EngineTail).
        bool  closeWithDeclick = false;
    };

    // Per bin entry: filled by the message thread before the entry is Parked, owned by the audio thread after.
    struct RetireState
    {
        int32_t samplesLeft;         // Fade: ramp frames left; gain at frame i = max(0, (samplesLeft - i) * gainStep)
        float   gainStep;            // 1 / rampFrames
        int64_t samplesLived;        // frames rendered since the tail started
        int64_t maxSamples;          // hard lifetime in frames
        uint8_t exclusive;           // 1: `out` holds nothing yet this block (no live layer, no earlier tail): the tail may write
        uint8_t started;             // onRetireStart has run
        uint8_t reserved[6];         // [0]: the bin armed a Natural tail's closing declick (the bin's own; a tail leaves it alone)
    };
    static_assert(std::is_trivially_copyable_v<RetireState>);

    // The bin's fade, applied by the tail itself at the point in its chain where its product applies it today
    // (mu-Tant fades the engine BEFORE its insert): gain at frame i = max(0, (samplesLeft - i) * gainStep),
    // mu-Tant's exact expression. The bin decrements samplesLeft by n after the call.
    inline void applyLinearFade(juce::AudioBuffer<float>& buf, int n, const RetireState& rs) noexcept
    {
        const int left = rs.samplesLeft;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* d = buf.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] *= juce::jmax(0.0f, (float) (left - i) * rs.gainStep);
        }
    }

    // A whole layer (type change, remove) or a product's engine tail (mu-Clid's hot-swap) as the bin holds it.
    class RetireTail
    {
    public:
        RetireTail() = default;
        virtual ~RetireTail() = default;
        // A copy is a new object: it is not inside a retire call, whatever the source was doing.
        RetireTail(const RetireTail&) noexcept {}
        RetireTail& operator=(const RetireTail&) noexcept { return *this; }

        // Any thread. Kind None (the default) means the layer is never parked.
        virtual RetirePolicy retirePolicy() const { return {}; }
        // Message thread, BEFORE any parameter of the slot changes and before the layer leaves the view: the
        // last moment it may read its bound parameters. Copy everything renderRetiring needs into the snapshot.
        virtual void beginRetire() {}
        // Audio thread, once, at the first tail block: freeze audio-owned state, emit owed note-offs into ctx.midiOut.
        virtual void onRetireStart(const RenderContext&) {}
        // Audio thread: ADD the tail into out[0, n) (or write it when rs.exclusive is 1); false once silent for good.
        virtual bool renderRetiring(const RenderContext&, juce::AudioBuffer<float>&, int, RetireState&) { return false; }

        // True while the bin is inside onRetireStart or renderRetiring of this object (audio thread only).
        // ParamBinding asserts on it: a tail that reads a parameter is a jassert in the first test that hits it.
        bool inRetirePath() const noexcept { return retirePath.load(std::memory_order_relaxed); }

    private:
        friend class RetireBin;
        // Written by the bin around the two tail calls (audio thread); an atomic so a read from another thread
        // (a stray ParamBinding::load) is a defined race, not undefined behaviour. Relaxed: it orders nothing.
        std::atomic<bool> retirePath { false };
    };

    // A layer type with a tail holds one of these as a member: plain data, written only by beginRetire and
    // read only by renderRetiring. Trivially copyable, so it cannot hold a live pointer into the parameters.
    template <class Snapshot>
    struct RetireSnapshot
    {
        static_assert(std::is_trivially_copyable_v<Snapshot>, "a retire snapshot is plain data, with no pointer into the APVTS");
        Snapshot value {};
    };

    // A bound parameter as a layer caches it in bindParams (render never looks a parameter up by id).
    // load() asserts that the read is not made from a tail call: a retiring layer reads its snapshot only.
    class ParamBinding
    {
    public:
        ParamBinding() = default;
        ParamBinding(std::atomic<float>* p, const RetireTail* owner) noexcept : param(p), tail(owner) {}

        float load(float fallback = 0.0f) const noexcept
        {
            jassert(tail == nullptr || ! tail->inRetirePath());
            return param != nullptr ? param->load(std::memory_order_relaxed) : fallback;
        }
        bool isBound() const noexcept { return param != nullptr; }

    private:
        std::atomic<float>* param = nullptr;
        const RetireTail*   tail  = nullptr;
    };
}
