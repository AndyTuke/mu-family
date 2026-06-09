#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/MuOnChannels.h"   // Channel enum
#include "Audio/KickEngine.h"
#include "Audio/BassEngine.h"
#include "Audio/SampleChannel.h"
#include "Audio/RumbleEngine.h"
#include "Sequencer/VoiceSlot.h"   // mu-core: per-lane ControlSequences + ModulationMatrix
#include "Modulation/LaneModulation.h"   // mu-core: shared range-based per-lane resolve
#include "Modulation/MuOnModDest.h"

#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>

// GrooveVoices — owns the five instrument engines (Kick/Bass/Hat/Snare + the Rumble lane,
// which processes the kick feed rather than stepping) and routes the sequencer's triggers +
// the mixer's per-channel render hook to the right one. Each block, every lane's engine
// params are resolved through that lane's modulation matrix: the current APVTS value is
// seeded as a 0..1 proportion, the lane's ControlSequences/assignments offset it, and the
// proportion is converted back to engine units. Lanes with no assignments round-trip the
// proportion unchanged (a no-op), so modulation is zero-cost when unused. All engine state
// is audio-thread-only; the cached pointers + VoiceSlots are written by the message thread.
namespace mu_on
{

class GrooveVoices
{
public:
    void prepare(double sr, int maxBlock)
    {
        kick.prepare(sr);
        bass.prepare(sr, maxBlock);
        hat.prepare(sr, maxBlock, SampleChannel::HiHat);
        snare.prepare(sr, maxBlock, SampleChannel::Snare);
        rumble.prepare(sr, maxBlock);
        kickFeed.setSize(2, juce::jmax(1, maxBlock));   // stash of the kick render → Rumble input
        kickFeed.clear();
    }

    // The processor owns the per-lane modulation slots; we read them on the audio thread.
    void setSlots(std::array<VoiceSlot, kNumChannels>* s) noexcept { slots = s; }

    // The Rumble lane's drawable bar-volume envelope (owned by the processor). Evaluated each
    // block under its try-lock; the message thread edits it while drawing.
    void setRumbleEnv(const ControlSequence* cs, CopyableSpinLock* lock) noexcept
        { rumbleEnvCs = cs; rumbleEnvLock = lock; }

    // Cache engine param pointers + per-lane modulation tables once (after the APVTS exists).
    void cacheParams(juce::AudioProcessorValueTreeState& apvts)
    {
        bWave = apvts.getRawParameterValue("b_wave");   // choice — not a mod destination
        jassert(bWave != nullptr);   // catch an id drift from the APVTS layout (debug)

        for (int lane = 0; lane < kNumChannels; ++lane)
        {
            int n = 0;
            const ModDestEntry* t = destsForLane(lane, n);
            auto& L = laneMod[(size_t) lane];
            L.count = n;
            L.map.reserve((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                L.ids[(size_t) i]    = t[i].propId;
                L.atoms[(size_t) i]  = apvts.getRawParameterValue(t[i].apvtsId);
                // A null atom means the dest's apvtsId drifted from the layout — the engine
                // would silently read a frozen range.start instead. Assert in debug.
                jassert(L.atoms[(size_t) i] != nullptr);
                L.ranges[(size_t) i] = apvts.getParameterRange(t[i].apvtsId);
                L.map.emplace(std::string_view(t[i].propId), 0.0f);   // pre-insert → no audio-thread alloc
            }
        }
    }

    // Silence all engines + clear the Rumble tails (e.g. on transport stop). Audio-thread safe.
    void reset()
    {
        kick.reset(); bass.reset(); hat.reset(); snare.reset();
        rumble.reset();
        kickFeed.clear();
    }

    // Resolve modulated params for every lane and push them into the engines (call once
    // per block, audio thread). `beat` is the song position the ControlSequences evaluate at;
    // `bpm` drives the Rumble's tempo-synced delay taps.
    void applyParams(double beat, double bpm)
    {
        blkBeat = beat;
        float kv[5] {}, bv[10] {}, hv[2] {}, sv[2] {}, rv[9] {};
        resolve(Kick,   kv);
        resolve(Bass,   bv);
        resolve(Hat,    hv);
        resolve(Snare,  sv);
        resolve(Rumble, rv);

        kick.setParams (kv[0], kv[1], kv[2], kv[3], kv[4]);
        bass.setParams (bv[0], (int) g(bWave, 0.0f), bv[1], bv[2], bv[3], bv[4], bv[5], bv[6], bv[7], bv[8], bv[9]);
        hat.setParams  (hv[0], hv[1]);
        snare.setParams(sv[0], sv[1]);
        // Rumble dest order: drive, 1/16, 2/16, 3/16, rev size, rev mix, rev LP, cutoff, resonance.
        rumble.setParams(bpm, rv[0], rv[1], rv[2], rv[3], rv[4], rv[5], rv[6], rv[7], rv[8]);

        // Evaluate the drawable bar-volume envelope (loops once per bar) under a try-lock;
        // on contention with a UI edit, reuse the last value. evaluate() returns 0..100.
        if (rumbleEnvCs != nullptr && rumbleEnvLock != nullptr)
        {
            bool expected = false;
            if (rumbleEnvLock->compare_exchange_strong(expected, true, std::memory_order_acquire))
            {
                lastRumbleEnv = juce::jlimit(0.0f, 1.0f, rumbleEnvCs->evaluate(beat) * 0.01f);
                rumbleEnvLock->store(false, std::memory_order_release);
            }
        }
        rumble.setBarEnvLevel(lastRumbleEnv);
    }

    void trigger(int track, float velocity, int sampleOffset = 0)
    {
        switch (track)
        {
            case Kick:  kick.trigger(velocity, sampleOffset);  break;
            case Bass:  bass.trigger(velocity, sampleOffset);  break;
            case Hat:   hat.trigger(velocity, sampleOffset);   break;
            case Snare: snare.trigger(velocity, sampleOffset); break;
            default: break;
        }
    }

    void render(int channel, juce::AudioBuffer<float>& buf, int n)
    {
        buf.clear();
        switch (channel)
        {
            case Kick:
                kick.render(buf, n);
                // Stash the kick render so the Rumble lane (rendered later, ch 4) can feed off
                // it. Channels render in index order, so the Kick (0) is ready before Rumble.
                for (int c = 0; c < juce::jmin(2, buf.getNumChannels()); ++c)
                    kickFeed.copyFrom(c, 0, buf, c, 0, juce::jmin(n, kickFeed.getNumSamples()));
                break;
            case Bass:   bass.render(buf, n);  break;
            case Hat:    hat.render(buf, n);   break;
            case Snare:  snare.render(buf, n); break;
            case Rumble: rumble.render(kickFeed, buf, n); break;   // processes the kick feed
            default: break;
        }
    }

private:
    static float g(const std::atomic<float>* p, float fallback) noexcept
    {
        return p != nullptr ? p->load(std::memory_order_relaxed) : fallback;
    }

    // Cached modulation data for one lane: the destination ids (proportion-space keys),
    // their backing APVTS atomics + ranges, and the reusable seed/result map.
    struct LaneMod
    {
        int count = 0;
        std::array<const char*, 16>                       ids   {};
        std::array<const std::atomic<float>*, 16>         atoms {};
        std::array<juce::NormalisableRange<float>, 16>    ranges{};
        std::unordered_map<std::string_view, float>       map;
    };

    // Resolve one lane's modulation via the shared mu-core range-based routing (seed
    // proportions → try-lock matrix → write engine-unit values back into `out`, in setParams
    // order). `out` is sized to the lane's dest count.
    void resolve(int lane, float* out)
    {
        auto& L = laneMod[(size_t) lane];
        VoiceSlot* slot = slots != nullptr ? &(*slots)[(size_t) lane] : nullptr;
        mu_mod::resolveLane(slot, blkBeat, L.count,
                            L.ids.data(), L.atoms.data(), L.ranges.data(), L.map, out);
    }

    KickEngine    kick;
    BassEngine    bass;
    SampleChannel hat, snare;
    RumbleEngine  rumble;
    juce::AudioBuffer<float> kickFeed;   // stash of the kick render → Rumble input

    std::array<VoiceSlot, kNumChannels>* slots = nullptr;
    const std::atomic<float>*            bWave = nullptr;
    double                               blkBeat = 0.0;

    const ControlSequence* rumbleEnvCs   = nullptr;   // Rumble bar-volume envelope (processor-owned)
    CopyableSpinLock*      rumbleEnvLock = nullptr;
    float                  lastRumbleEnv = 1.0f;       // reused on lock contention

    LaneMod laneMod[kNumChannels];
};

} // namespace mu_on
