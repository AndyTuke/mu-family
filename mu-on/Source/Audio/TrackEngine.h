#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Persistence/TrackParams.h"   // EngineType
#include "Audio/KickEngine.h"
#include "Audio/BassEngine.h"
#include "Audio/SampleChannel.h"
#include "Audio/RumbleEngine.h"
#include "Audio/MusicalScales.h"   // mu-core: snapMidiToScale for the Kick / Bass tune lock
#include "Audio/FmPercEngine.h"
#include "Audio/ChordEngine.h"

#include <variant>

// TrackEngine — the one sound engine a track runs (Kick, Bass, Hat, Snare, Rumble, FM Perc or Chord), behind one interface so
// the track can render whichever it holds. A track owns exactly one (docs/mu-tronic/extraction-plan.md E3.5):
// changing the engine builds a new TrackEngine on the message thread and swaps it in (Track::installEngine),
// so nothing here is ever constructed, prepared or destroyed on the audio thread.
//
// Threads: construct and prepare on the message thread (or the host's prepare thread, not processing); every
// other call is the audio thread's, inside the owning track's engine lock.
namespace mu_on
{

// The master key and scale a Kick or Bass tune is locked to, and each engine's Fine offset. Default: C chromatic, so a
// host with no key of its own (mu-Tronic today) snaps to the nearest semitone.
struct TuneLock
{
    int   root  = 0;
    int   scale = mu_audio::kNumScales - 1;
    float kickFineCents = 0.0f, bassFineCents = 0.0f;

    // `hz` snapped to the nearest note of the key and scale, then moved by `fineCents`.
    float lockedHz(float hz, float fineCents) const noexcept
    {
        const float midi = 69.0f + 12.0f * std::log2(juce::jmax(1.0f, hz) / 440.0f);
        return mu_audio::midiToFreq(mu_audio::snapMidiToScale(midi, root, scale) + fineCents * 0.01f);
    }

    // A Bass tune: the key's root note while the Tune sits at its default, else lockedHz.
    float lockedBassHz(float hz, float fineCents) const noexcept
    {
        if (! isBassTuneRoot(hz)) return lockedHz(hz, fineCents);
        return mu_audio::midiToFreq((float) bassRootMidi(root) + fineCents * 0.01f);
    }
};

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
            case EngineType::FmPerc: engine.emplace<FmPercEngine>();  break;
            case EngineType::Chord:  engine.emplace<ChordEngine>();   break;
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
        else if (auto* f = std::get_if<FmPercEngine>(&engine))  f->prepare(sampleRate, maxBlock);
        else if (auto* c = std::get_if<ChordEngine>(&engine))   c->prepare(sampleRate, maxBlock);
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
        else if (auto* c = std::get_if<ChordEngine>(&engine)) c->resetFilterSmoothing();
    }

    // This block's parameters, in the order of the engine's modulation target table (MuOnModDest.h). `engineChoice` is
    // the engine's choice row, which is not a target (the Bass wave, or the FM Perc algorithm); `lock` snaps the Kick and Bass tune to the master key and scale; `bpm` and `rumbleEnvLevel` feed the Rumble's tempo-synced taps and its
    // bar envelope.
    void setParams(const float* p, int engineChoice, double bpm, float rumbleEnvLevel, const TuneLock& lock = {}) noexcept
    {
        if (auto* k = std::get_if<KickEngine>(&engine))         k->setParams(lock.lockedHz(p[0], lock.kickFineCents), p[1], p[2], p[3], p[4]);
        else if (auto* b = std::get_if<BassEngine>(&engine))    b->setParams(lock.lockedBassHz(p[0], lock.bassFineCents), engineChoice, p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
        else if (auto* s = std::get_if<SampleChannel>(&engine)) s->setParams(p[0], p[1], p[2], p[3]);   // the Hat's open pair follows the closed pair; a Snare ignores it
        else if (auto* r = std::get_if<RumbleEngine>(&engine))
        {
            // Rumble dest order: drive, 1/16, 2/16, 3/16, rev size, rev mix, rev LP, cutoff, resonance, input LP.
            r->setParams(bpm, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
            r->setBarEnvLevel(rumbleEnvLevel);
        }
        else if (auto* f = std::get_if<FmPercEngine>(&engine))
            f->setParams(engineChoice, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        else if (auto* ch = std::get_if<ChordEngine>(&engine))   // Chord dest order: cutoff, resonance, drive, filter env, env decay, attack, decay, sustain, release, filter env attack, sustain, release
            ch->setParams(engineChoice, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], lock.root, lock.scale);
    }

    // Fire the engine at `sampleOffset` in the block (a Rumble does not step). `open` is the step's Open flag: only the Hat uses it.
    void trigger(float velocity, int sampleOffset, bool open = false, bool tied = false, int gateSamples = 0, float semitones = 0.0f,
                 const ChordHit* chordHit = nullptr) noexcept
    {
        if (auto* k = std::get_if<KickEngine>(&engine))         k->trigger(velocity, sampleOffset, semitones);
        else if (auto* b = std::get_if<BassEngine>(&engine))    b->trigger(velocity, sampleOffset, tied, gateSamples, semitones);
        else if (auto* s = std::get_if<SampleChannel>(&engine)) s->trigger(velocity, sampleOffset, open);
        else if (auto* f = std::get_if<FmPercEngine>(&engine))  f->trigger(velocity, sampleOffset);
        else if (auto* c = std::get_if<ChordEngine>(&engine))   c->trigger(velocity, sampleOffset, chordHit != nullptr ? *chordHit : ChordHit {}, gateSamples);
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
    std::variant<KickEngine, BassEngine, SampleChannel, RumbleEngine, FmPercEngine, ChordEngine> engine;
};

} // namespace mu_on
