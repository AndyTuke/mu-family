#pragma once

#include "Sequencer/ArpVoiceRunner.h"   // per-layer arpeggiator + ToniVoice
#include "Sequencer/HeldNotes.h"        // the keys this layer hears
#include "Sequencer/Layer.h"            // mu-core: name, colour, modulation, the render contract
#include "Sequencer/LayerTypeInfo.h"    // mu-core: mu_core::LayerTypeId
#include "Sequencer/RetireTail.h"       // mu-core: mu_core::ParamBinding
#include "Audio/InsertProcessor.h"      // mu-core: shared per-voice insert FX
#include "Modulation/MuToniModDest.h"   // isValidModDest, the modulation target table
#include "Persistence/ArpParams.h"      // the Arp type's parameter table (vpi)

#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>

namespace mu_toni
{

// One mu-Toni layer: the shared Layer (name, colour, modulation) plus its own arp voice and insert effect,
// rendered through the shared layer contract (docs/mu-tronic/design.md 2.1): bindParams caches the slot's
// parameter atomics, render resolves the modulation, runs the arp and the voice and applies the insert.
//
// The keys it hears (root-by-MIDI, MIDI-trigger) are its own state: `heldNotes`, and `context`, the block's
// arp context the runner renders with. Threads: everything below is written and read by the audio thread
// only, inside the block's layer access (render, onParkedBlock), with three exceptions that run while no
// block can render this layer: prepare (the host is not processing), bindParams (before the layer is
// published, or while layerEditInFlight keeps every block silent during an edit) and the processor's
// layersMoved (under layersLock, which every rendered block holds in TryLock mode), which resets the runner
// and hands a new Arp the keys already held. The UI reads only the modLive atomics.
struct Arp : Layer
{
    ArpVoiceRunner  runner;
    SlotBound<InsertProcessor> insert;   // belongs to the slot: a layer copy never carries its delay lines
    VoiceParams     insertCfg;
    HeldNotes       heldNotes;           // the keys this layer hears as held, newest on top
    ArpContext      context;             // this block's arp context: the transport plus this layer's held keys

    // The compressor / limiter meter of this layer's insert.
    const std::atomic<float>* insertGainReduction() const override { return &insert->grReduction; }

    // Saved modulators keep only destinations mu-Toni has.
    bool isValidDest(const std::string& id) const override { return isValidModDest(id); }

    // ── The render contract ──────────────────────────────────────────────────
    int  typeId() const override { return (int) mu_core::LayerTypeId::Arp; }
    // Message thread, not processing: the voice's and the insert's buffers for this rate and block size. The
    // next rendered block starts the runner from silence (a prepare, like a slot going live, is a fresh start).
    void prepare(double sampleRate, int blockSize) override;
    // Message thread: the slot's a{slot}_ rows, the MIDI-in channel row and the modulation inputs.
    void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override;
    // Audio thread: the layer's block, insert included, into out[0, n) (handed over cleared).
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;
    // Audio thread: a parked spare keeps hearing the keys, so it knows them when its slot goes live.
    void onParkedBlock(const mu_core::RenderContext& ctx) override;
    // A staged layer preset lands on the 4/4 bar line (the arp has no loop of its own).
    double loopBeats() const override { return 4.0; }

    // ── How the layer leaves its slot ────────────────────────────────────────
    // Fade (the family defaults, 25 ms and 2 s): the voice and the insert keep the parameters they were last
    // given, so the tail copies nothing at beginRetire and never reads a parameter.
    mu_core::RetirePolicy retirePolicy() const override
    {
        mu_core::RetirePolicy p;
        p.kind = mu_core::RetirePolicy::Kind::Fade;
        return p;
    }
    // Audio thread: the voice (no keys, no steps: its held note is released) and then the insert, under the bin's
    // linear fade. False never: the bin ends the tail when its ramp is spent.
    bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n, mu_core::RetireState& rs) override;

    // ── The block's work in parts ────────────────────────────────────────────
    // Audio thread: the call's MIDI (its own frames) into the held keys, and the key fields of `context` from
    // them. The view's first ctx.midi.carried events are from earlier blocks that rendered nothing: their keys
    // count, their presses do not (no fresh MIDI-trigger restart).
    void hearMidi(const mu_core::RenderContext& ctx) noexcept;
    // Audio thread: the MIDI-in channel row (0 = Omni).
    int midiInChannel() const noexcept { return juce::roundToInt(midiInParam.load(0.0f)); }
    // Audio thread: resolve this layer's modulation at `modBeat` and push every parameter into the runner and
    // the insert configuration; publishes the post-matrix values (getModLive).
    void readParams(double modBeat) noexcept;
    // Audio thread: the runner's block, then the insert (engine -> insert -> mixer).
    void renderBlock(juce::AudioBuffer<float>& buf, int n) noexcept
    {
        runner.render(buf, n, context);
        insert->process(buf, n, buf.getNumChannels(), insertCfg);
    }

    // Post-matrix value of modulation target `dest`, in the target's parameter units (any thread).
    float getModLive(int dest) const noexcept
    {
        return modLive.v[(size_t) juce::jlimit(0, kNumModDests - 1, dest)].load(std::memory_order_relaxed);
    }

private:
    // The UI's live modulation values: written by the audio thread, read by the knobs' arcs on their own
    // cache line (C16). A copy of the layer starts with fresh ones (atomics do not copy).
    struct LiveValues
    {
        alignas(64) std::array<std::atomic<float>, kNumModDests> v {};
        LiveValues() = default;
        LiveValues(const LiveValues&) noexcept {}
        LiveValues& operator=(const LiveValues&) noexcept { return *this; }
    };

    std::array<mu_core::ParamBinding, kNumVoiceParams> vp {};   // the slot's rows, in vpi order
    mu_core::ParamBinding midiInParam;                           // the MIDI-in channel row (one for every slot)

    // Modulation-resolve inputs: parallel arrays for mu_mod::resolveLane, and its work map (pre-sized).
    std::array<const std::atomic<float>*, kNumModDests>       modDestAtoms {};
    std::array<const char*, kNumModDests>                     modDestIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests>  modDestRanges {};
    std::unordered_map<std::string_view, float>               modParamValues;
    LiveValues                                                modLive;
    std::array<float, kNumModDests>                           lastResolved {};   // the last block's out[] (C12)
    bool                                                      haveResolved = false;

    juce::AudioBuffer<float> tailScratch;   // a tail that must add rather than write renders here (sized at prepare)

    bool parked = true;   // not rendered live in the last rendered block (or prepared since): start from silence
};

} // namespace mu_toni
