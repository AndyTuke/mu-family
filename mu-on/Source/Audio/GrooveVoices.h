#pragma once

#include "Audio/SpinLock.h"   // mu-core: spin lock helpers
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/TrackParams.h"   // EngineType, parameter ids
#include "Audio/KickEngine.h"
#include "Audio/BassEngine.h"
#include "Audio/SampleChannel.h"
#include "Audio/RumbleEngine.h"
#include "Sequencer/Track.h"   // mu-On layer (mu-core Layer + step row + Rumble envelope)
#include "Modulation/LaneModulation.h"   // mu-core: shared range-based per-layer resolve
#include "Modulation/MuOnModDest.h"

#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>

// GrooveVoices — owns one engine of each type for every layer slot (Kick, Bass, Hat, Snare, and the
// Rumble processor, which works on another layer's audio rather than stepping) and routes the
// sequencer's triggers + the mixer's per-channel render hook to the engine each layer has selected.
// Each block, the selected engine's params are resolved through that layer's modulation matrix: the
// current APVTS value is seeded as a 0..1 proportion, the layer's ControlSequences/assignments offset
// it, and the proportion is converted back to engine units. Layers with no assignments round-trip the
// proportion unchanged (a no-op), so modulation is zero-cost when unused. All engine state is
// audio-thread-only.
//
// The engines belong to the slot, not to the layer object, so a structural edit moves the layers under
// them: the processor calls resetSlot() for each moved slot. The Track pointers handed in by
// beginBlock() are valid for that block only (the processor takes them under its layer lock each
// block); nothing here keeps a layer across blocks.
//
// Render order: every layer that is not Rumble first, then every Rumble layer, so the layer a Rumble
// processes has always been rendered this block, wherever it sits in the layer order. A Rumble whose
// source is out of range, not live, or itself a Rumble processes silence.
namespace mu_on
{

class GrooveVoices
{
public:
    static constexpr int kSlots = mu_limits::kMaxLayers;

    void prepare(double sr, int maxBlock)
    {
        for (int s = 0; s < kSlots; ++s)
        {
            auto& v = slotVoices[(size_t) s];
            v.kick.prepare(sr);
            v.bass.prepare(sr, maxBlock);
            v.hat.prepare(sr, maxBlock, SampleChannel::Kind::HiHat);
            v.snare.prepare(sr, maxBlock, SampleChannel::Kind::Snare);
            v.rumble.prepare(sr, maxBlock);
            out[(size_t) s].setSize(2, juce::jmax(1, maxBlock));
            out[(size_t) s].clear();
            current[(size_t) s] = -1;
        }
        silence.setSize(2, juce::jmax(1, maxBlock));
        silence.clear();
        activeCount = 0;
    }

    // Cache every slot's engine params + modulation tables once (after the APVTS exists).
    void cacheParams(juce::AudioProcessorValueTreeState& apvts)
    {
        for (int s = 0; s < kSlots; ++s)
        {
            engineParam[(size_t) s] = apvts.getRawParameterValue(layerParamId(s, "eng"));
            srcParam[(size_t) s]    = apvts.getRawParameterValue(layerParamId(s, "src"));
            bassWave[(size_t) s]    = apvts.getRawParameterValue(engineParamId(s, EngineType::Bass, "wave"));   // choice, not a mod destination
            jassert(engineParam[(size_t) s] != nullptr && srcParam[(size_t) s] != nullptr && bassWave[(size_t) s] != nullptr);   // an id drift from the APVTS layout (debug)

            for (int e = 0; e < kNumEngineTypes; ++e)
            {
                const auto engine = engineFromIndex(e);
                int n = 0;
                const ModDestEntry* t = destsForEngine(engine, n);
                auto& L = mods[(size_t) s][(size_t) e];
                L.count = n;
                L.map.reserve((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const auto id = layerParamPrefix(s) + t[i].param;
                    L.ids[(size_t) i]    = t[i].id;
                    L.atoms[(size_t) i]  = apvts.getRawParameterValue(id);
                    // A null atom means the target's param drifted from the layout — the engine
                    // would silently read a frozen range.start instead. Assert in debug.
                    jassert(L.atoms[(size_t) i] != nullptr);
                    L.ranges[(size_t) i] = apvts.getParameterRange(id);
                    L.map.emplace(std::string_view(t[i].id), 0.0f);   // pre-insert → no audio-thread alloc
                }
            }
        }
    }

    // Start of a block: the live layers' Track pointers (read under the processor's layer lock, valid
    // for this block only). A slot that has just become live starts from silence, so a tail left by an
    // earlier tenant (a sustaining bass has no note-off) never resumes.
    void beginBlock(Track* const* tracks, int count) noexcept
    {
        count = juce::jlimit(0, kSlots, count);
        for (int s = 0; s < kSlots; ++s) snapshot[(size_t) s] = s < count ? tracks[s] : nullptr;
        for (int s = activeCount; s < count; ++s) resetSlot(s);
        activeCount = count;
    }

    // Silence one slot's engines (a structural edit changed which layer sits there). Audio-thread
    // state only, so it is safe wherever the processor's layer lock is held. No allocation.
    void resetSlot(int s) noexcept
    {
        if (s < 0 || s >= kSlots) return;
        silenceSlot(s);
        current[(size_t) s] = -1;
    }

    // Silence all engines + clear the Rumble tails (e.g. on transport stop). Audio-thread safe.
    void reset() noexcept
    {
        for (int s = 0; s < kSlots; ++s) silenceSlot(s);
    }

    // Resolve modulated params for every live layer's selected engine and push them in (call once
    // per block after beginBlock, audio thread). `beat` is the song position the ControlSequences
    // evaluate at; `bpm` drives the Rumble's tempo-synced delay taps.
    void applyParams(double beat, double bpm)
    {
        blkBeat = beat;
        for (int s = 0; s < activeCount; ++s)
        {
            const int e = engineIndexOf(s);

            // The layer changed engine (host automation, a preset): the old engine stops dead.
            if (current[(size_t) s] != e)
            {
                resetSlot(s);
                current[(size_t) s] = e;
            }

            auto& v = slotVoices[(size_t) s];
            float p[10] {};
            resolve(s, e, p);
            switch (engineFromIndex(e))
            {
                case EngineType::Kick:  v.kick.setParams(p[0], p[1], p[2], p[3], p[4]); break;
                case EngineType::Bass:  v.bass.setParams(p[0], (int) g(bassWave[(size_t) s], 0.0f), p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]); break;
                case EngineType::Hat:   v.hat.setParams(p[0], p[1]); break;
                case EngineType::Snare: v.snare.setParams(p[0], p[1]); break;
                case EngineType::Rumble:
                    // Rumble dest order: drive, 1/16, 2/16, 3/16, rev size, rev mix, rev LP, cutoff, resonance.
                    v.rumble.setParams(bpm, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
                    v.rumble.setBarEnvLevel(evaluateRumbleEnv(s, beat));
                    break;
            }
        }
    }

    // Post-matrix value of modulation target `dest` (a row of the engine's table) on layer `slot`, in the
    // target knob's parameter units, as the last block resolved it; NaN while the slot runs another
    // engine than `engine`. Read by the knobs' live arcs (message thread).
    float getModLive(int slot, EngineType engine, int dest) const noexcept
    {
        if (slot < 0 || slot >= kSlots || dest < 0 || dest >= kMaxDests)
            return std::numeric_limits<float>::quiet_NaN();
        if (modLiveEngine[(size_t) slot].load(std::memory_order_relaxed) != toIndex(engine))
            return std::numeric_limits<float>::quiet_NaN();
        return modLive[(size_t) slot][(size_t) dest].load(std::memory_order_relaxed);
    }

    // Fire layer `slot`'s engine (a Rumble layer does not step).
    void trigger(int slot, float velocity, int sampleOffset = 0)
    {
        if (slot < 0 || slot >= activeCount) return;
        auto& v = slotVoices[(size_t) slot];
        if (current[(size_t) slot] < 0) return;   // no block has resolved this slot's engine yet
        switch (engineFromIndex(current[(size_t) slot]))
        {
            case EngineType::Kick:   v.kick.trigger(velocity, sampleOffset);  break;
            case EngineType::Bass:   v.bass.trigger(velocity, sampleOffset);  break;
            case EngineType::Hat:    v.hat.trigger(velocity, sampleOffset);   break;
            case EngineType::Snare:  v.snare.trigger(velocity, sampleOffset); break;
            case EngineType::Rumble: break;
        }
    }

    // Render every live layer into its own buffer, Rumble layers last (see the class comment).
    void renderAll(int n)
    {
        n = juce::jmin(n, silence.getNumSamples());
        for (int pass = 0; pass < 2; ++pass)
            for (int s = 0; s < activeCount; ++s)
            {
                const bool isRumble = current[(size_t) s] == toIndex(EngineType::Rumble);
                if (isRumble != (pass == 1)) continue;

                auto& buf = out[(size_t) s];
                auto& v   = slotVoices[(size_t) s];
                buf.clear();
                if (current[(size_t) s] < 0) continue;
                switch (engineFromIndex(current[(size_t) s]))
                {
                    case EngineType::Kick:   v.kick.render(buf, n);  break;
                    case EngineType::Bass:   v.bass.render(buf, n);  break;
                    case EngineType::Hat:    v.hat.render(buf, n);   break;
                    case EngineType::Snare:  v.snare.render(buf, n); break;
                    case EngineType::Rumble: v.rumble.render(rumbleInput(s), buf, n, ! rumbleHasSource(s)); break;
                }
            }
    }

    // The mixer's per-channel render hook: layer `slot`'s block, rendered by renderAll.
    void copyOut(int slot, juce::AudioBuffer<float>& buf, int n) const
    {
        buf.clear();
        if (slot < 0 || slot >= activeCount) return;
        const auto& src = out[(size_t) slot];
        const int ns = juce::jmin(n, src.getNumSamples(), buf.getNumSamples());
        for (int c = 0; c < buf.getNumChannels(); ++c)
            buf.copyFrom(c, 0, src, juce::jmin(c, src.getNumChannels() - 1), 0, ns);
    }

private:
    struct SlotVoices
    {
        KickEngine    kick;
        BassEngine    bass;
        SampleChannel hat, snare;
        RumbleEngine  rumble;
    };

    // Cached modulation data for one slot + engine: the destination ids (proportion-space keys),
    // their backing APVTS atomics + ranges, and the reusable seed/result map.
    static constexpr int kMaxDests = 16;   // the most targets any engine's table holds (Bass: 10)

    struct EngineMod
    {
        int count = 0;
        std::array<const char*, kMaxDests>                       ids   {};
        std::array<const std::atomic<float>*, kMaxDests>         atoms {};
        std::array<juce::NormalisableRange<float>, kMaxDests>    ranges{};
        std::unordered_map<std::string_view, float>       map;
    };

    void silenceSlot(int s) noexcept
    {
        auto& v = slotVoices[(size_t) s];
        v.kick.reset(); v.bass.reset(); v.hat.reset(); v.snare.reset(); v.rumble.reset();
        out[(size_t) s].clear();
    }

    static float g(const std::atomic<float>* p, float fallback) noexcept
    {
        return p != nullptr ? p->load(std::memory_order_relaxed) : fallback;
    }

    int engineIndexOf(int s) const noexcept
    {
        return juce::jlimit(0, kNumEngineTypes - 1, juce::roundToInt(g(engineParam[(size_t) s], 0.0f)));
    }

    // Resolve slot `s`'s engine `e` via the shared mu-core range-based routing (seed proportions →
    // try-lock matrix → write engine-unit values back into `out`, in setParams order).
    void resolve(int s, int e, float* outValues)
    {
        auto& L = mods[(size_t) s][(size_t) e];
        mu_mod::resolveLane(snapshot[(size_t) s], blkBeat, L.count,
                            L.ids.data(), L.atoms.data(), L.ranges.data(), L.map, outValues);

        // Publish the post-matrix values for the knobs' live modulation arcs.
        for (int k = 0; k < L.count; ++k)
            modLive[(size_t) s][(size_t) k].store(outValues[k], std::memory_order_relaxed);
        modLiveEngine[(size_t) s].store(e, std::memory_order_relaxed);
    }

    // The Rumble envelope level (0..1) at `beat`, evaluated under its try-lock; on contention with a
    // UI edit, the last value stands.
    float evaluateRumbleEnv(int s, double beat)
    {
        auto* track = snapshot[(size_t) s];
        if (track == nullptr) return 1.0f;
        if (mu_core::trySpinLock(track->rumbleEnvLock))
        {
            lastRumbleEnv[(size_t) s] = juce::jlimit(0.0f, 1.0f, track->rumbleEnv.evaluate(beat) * 0.01f);
            mu_core::spinUnlock(track->rumbleEnvLock);
        }
        return lastRumbleEnv[(size_t) s];
    }

    // What Rumble layer `s` processes: its source layer's block, or silence when the source is not a
    // live layer, is the Rumble itself, or is another Rumble.
    const juce::AudioBuffer<float>& rumbleInput(int s) const noexcept
    {
        const int src = rumbleSource(s);
        return src < 0 ? silence : out[(size_t) src];
    }

    bool rumbleHasSource(int s) const noexcept { return rumbleSource(s) >= 0; }

    // The live non-Rumble layer Rumble `s` processes, or -1 when its source is not usable.
    int rumbleSource(int s) const noexcept
    {
        const int src = juce::roundToInt(g(srcParam[(size_t) s], 0.0f));
        if (src < 0 || src >= activeCount || src == s) return -1;
        if (current[(size_t) src] == toIndex(EngineType::Rumble) || current[(size_t) src] < 0) return -1;
        return src;
    }

    std::array<SlotVoices, kSlots>                          slotVoices;
    std::array<juce::AudioBuffer<float>, kSlots>            out;           // each live layer's rendered block
    juce::AudioBuffer<float>                                silence;       // a Rumble with no source reads this
    std::array<std::array<EngineMod, kNumEngineTypes>, kSlots> mods;
    std::array<std::array<std::atomic<float>, kMaxDests>, kSlots> modLive {};   // post-matrix target values (audio thread writes, UI reads)
    std::array<std::atomic<int>, kSlots>                          modLiveEngine {};   // the engine those values belong to

    std::array<const std::atomic<float>*, kSlots> engineParam {}, srcParam {}, bassWave {};
    std::array<Track*, kSlots>                    snapshot {};              // this block's live layers (valid for one block)
    std::array<int, kSlots>                       current {};               // the engine each slot last ran (-1 = none yet)
    std::array<float, kSlots>                     lastRumbleEnv {};         // reused on lock contention
    int    activeCount = 0;
    double blkBeat     = 0.0;

public:
    GrooveVoices()
    {
        current.fill(-1);
        lastRumbleEnv.fill(1.0f);
        for (auto& e : modLiveEngine) e.store(-1, std::memory_order_relaxed);   // no values published yet
    }
};

} // namespace mu_on
