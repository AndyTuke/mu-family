#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Persistence/TrackParams.h"   // EngineType
#include "Audio/KickEngine.h"
#include "Audio/BassEngine.h"
#include "Audio/SampleChannel.h"
#include "Audio/RumbleEngine.h"

#include <variant>

// TrackEngine — the one sound engine a track runs (Kick, Bass, Hat, Snare or Rumble), behind one interface so
// the track can render whichever it holds. A track owns exactly one (docs/mu-tronic/extraction-plan.md E3.5):
// changing the engine builds a new TrackEngine on the message thread and swaps it in (Track::installEngine),
// so nothing here is ever constructed, prepared or destroyed on the audio thread.
//
// Threads: construct and prepare on the message thread (or the host's prepare thread, not processing); every
// other call is the audio thread's, inside the owning track's engine lock.
namespace mu_on
{

class TrackEngine
{
public:
    explicit TrackEngine(EngineType t) : kind(t)
    {
        switch (t)
        {
            case EngineType::Kick:   engine.emplace<KickEngine>();    break;
            case EngineType::Bass:   engine.emplace<BassEngine>();    break;
            case EngineType::Hat:
            case EngineType::Snare:  engine.emplace<SampleChannel>(); break;
            case EngineType::Rumble: engine.emplace<RumbleEngine>();  break;
        }
    }

    EngineType type() const noexcept { return kind; }

    // Message thread, not processing: the engine's buffers for this rate and largest block (allocates).
    void prepare(double sampleRate, int maxBlock)
    {
        if (auto* k = std::get_if<KickEngine>(&engine))         k->prepare(sampleRate);
        else if (auto* b = std::get_if<BassEngine>(&engine))    b->prepare(sampleRate, maxBlock);
        else if (auto* s = std::get_if<SampleChannel>(&engine)) s->prepare(sampleRate, maxBlock, kind == EngineType::Hat ? SampleChannel::Kind::HiHat
                                                                                                                         : SampleChannel::Kind::Snare);
        else if (auto* r = std::get_if<RumbleEngine>(&engine))  r->prepare(sampleRate, maxBlock);
    }

    // Silence the voice at once: transport stop, a slot going live, an engine change. Allocation-free.
    void reset() noexcept
    {
        std::visit([](auto& e) { e.reset(); }, engine);
    }

    // For a track that moved to another slot (a structural edit, under layersLock): reset, and the Bass filter's
    // drive smoothing restarted from 0 as a freshly prepared filter's is, so the track starts in its new slot as
    // a first tenant of that slot did when the engines belonged to the slot. Allocation-free. Not reproduced: the
    // Rumble reverb's parameter glide, which only a newly built reverb restarts (a moved Rumble's reverb keeps
    // its settled parameters).
    void resetToPrepared() noexcept
    {
        reset();
        if (auto* b = std::get_if<BassEngine>(&engine)) b->resetFilterSmoothing();
    }

    // This block's parameters, in the order of the engine's modulation target table (MuOnModDest.h). `bassWave` is
    // the Bass wave choice (not a target); `bpm` and `rumbleEnvLevel` feed the Rumble's tempo-synced taps and its
    // bar envelope.
    void setParams(const float* p, int bassWave, double bpm, float rumbleEnvLevel) noexcept
    {
        if (auto* k = std::get_if<KickEngine>(&engine))         k->setParams(p[0], p[1], p[2], p[3], p[4]);
        else if (auto* b = std::get_if<BassEngine>(&engine))    b->setParams(p[0], bassWave, p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        else if (auto* s = std::get_if<SampleChannel>(&engine)) s->setParams(p[0], p[1]);
        else if (auto* r = std::get_if<RumbleEngine>(&engine))
        {
            // Rumble dest order: drive, 1/16, 2/16, 3/16, rev size, rev mix, rev LP, cutoff, resonance.
            r->setParams(bpm, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
            r->setBarEnvLevel(rumbleEnvLevel);
        }
    }

    // Fire the engine at `sampleOffset` in the block (a Rumble does not step).
    void trigger(float velocity, int sampleOffset) noexcept
    {
        if (auto* k = std::get_if<KickEngine>(&engine))         k->trigger(velocity, sampleOffset);
        else if (auto* b = std::get_if<BassEngine>(&engine))    b->trigger(velocity, sampleOffset);
        else if (auto* s = std::get_if<SampleChannel>(&engine)) s->trigger(velocity, sampleOffset);
    }

    // Add the engine's block into `out` (handed over cleared). A Rumble processes `rumbleInput`, its source's
    // block (`rumbleHasSource` false: a silent stand-in, so its tail can die away).
    void render(juce::AudioBuffer<float>& out, int n, const juce::AudioBuffer<float>& rumbleInput, bool rumbleHasSource)
    {
        if (auto* r = std::get_if<RumbleEngine>(&engine)) { r->render(rumbleInput, out, n, ! rumbleHasSource); return; }
        std::visit([&out, n](auto& e)
        {
            if constexpr (! std::is_same_v<std::decay_t<decltype(e)>, RumbleEngine>) e.render(out, n);
        }, engine);
    }

private:
    EngineType kind;
    std::variant<KickEngine, BassEngine, SampleChannel, RumbleEngine> engine;
};

} // namespace mu_on
