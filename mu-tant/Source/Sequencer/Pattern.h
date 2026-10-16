#pragma once

#include "Sequencer/Layer.h"          // mu-core: name, colour, modulation, the render contract
#include "Sequencer/LayerTypeInfo.h"  // mu-core: mu_core::LayerTypeId
#include "Sequencer/RetireTail.h"     // mu-core: ParamBinding, RetireSnapshot
#include "Sequencer/GatePattern.h"    // the drawable gate / filter / pitch envelopes
#include "Sequencer/HeldNoteStack.h"  // Note mode's held keys
#include "Audio/SynthVoice.h"         // VoiceEngine, VoiceConfig
#include "Audio/VoiceRingBuffer.h"    // audio -> UI level tap for the sidebar glyph
#include "Audio/InsertProcessor.h"    // mu-core: shared per-voice insert FX
#include "Modulation/MuTantModDest.h" // the modulation target table
#include "Modulation/MuTantModSnap.h" // the live-arc snapshot indices

#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>

namespace mu_tant
{

class PatternHost;

// One mu-Tant layer (a drone voice): the shared Layer (name, colour, modulation) plus everything
// that belongs to this voice alone — its three drawable envelopes, its user wavetables and the
// audio-to-UI level tap. The sound's parameters live in the APVTS (v{N}_*).
//
// A structural edit (add / remove / reorder) moves whole Patterns by pointer, so everything here
// travels with its voice. Copying one carries the voice's DATA — modulation, envelopes, user
// wavetable choice — and leaves the destination's own state alone (level tap, the stepped-pitch
// flags, the host link, every render member below: the copy constructor default-builds them and the
// assignment leaves them); a reset takes a default Pattern's data in place (adoptDefaults).
//
// It renders through the shared layer contract (docs/mu-tronic/design.md 2.1). Threads: bindParams, prepare,
// beginRetire, setNoteMode and takeSlotState run on the message thread while no block can render the layer
// (before publication, while the host is not processing, under layersLock or with an edit in flight), except
// beginRetire and setNoteMode, which only read bound atomics or write one; render, onParkedBlock and
// renderRetiring run on the audio thread inside the block's layer access. The UI reads only the atomics
// (modLive, the flags, the ring).
// The live modulation values sit on their own cache line (alignas), which pads the struct on purpose.
JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4324)
struct Pattern : Layer
{
    Pattern() = default;
    Pattern(const Pattern& o) : Layer(o) { copyVoiceData(o); }
    Pattern& operator=(const Pattern& o)
    {
        if (this != &o) { Layer::operator=(o); copyVoiceData(o); }
        return *this;
    }

    GatePattern gate;          // amplitude gate
    GatePattern filterGate;    // filter-cutoff envelope
    GatePattern pitchGate;     // pitch envelope

    // User wavetable per oscillator: the file path (shown / saved) and its bank index (-1 = the
    // factory selection). The index is read by the audio thread, so it is atomic.
    juce::String     osc1UserPath, osc2UserPath;
    std::atomic<int> osc1UserIndex { -1 }, osc2UserIndex { -1 };

    // Whether a stepped (quantised) source drives each oscillator's semitone (audio-thread flag,
    // refreshed whenever the voice's modulators change).
    std::atomic<bool> osc1SemiStepped { false }, osc2SemiStepped { false };

    VoiceRingBuffer ring;      // mono tap of the voice's output for the sidebar animation

    // The voice's insert effect and its delay lines. It belongs to the slot, not to the data, so a
    // copy or adoptDefaults leaves it alone; a pointer move carries it, and the processor re-seeds
    // and clears it for the new slot (layersMoved).
    SlotBound<InsertProcessor> insert;
    const std::atomic<float>* insertGainReduction() const override { return &insert->grReduction; }

    // The layer's host (the wavetable bank a saved path resolves into, the slot's engine); a copy keeps its own link.
    OwnerLink<PatternHost> host;
    void writeExtras(juce::ValueTree& node) const override;
    void applyExtras(const juce::ValueTree& node) override;

    // Recompute the stepped-pitch flags from the layer's own modulators. Message thread, after any modulator
    // change (editor edit, preset load, structural edit); the audio thread reads the flags lock-free.
    void refreshPitchQuantFlags();

    // Takes a default voice's data (built before the structural edit locked), keeping name and
    // colour; in place, so the editor's pointers into this Pattern stay valid. Exchanges rather than
    // copies, so it allocates and frees nothing under the layers lock: the old envelopes and
    // wavetable paths leave in `fresh`, which the caller destroys after unlocking.
    void adoptDefaults(Layer& fresh) override
    {
        Layer::adoptDefaults(fresh);
        jassert(dynamic_cast<Pattern*>(&fresh) != nullptr);
        auto& f = static_cast<Pattern&>(fresh);
        adoptGate(gate, f.gate);
        adoptGate(filterGate, f.filterGate);
        adoptGate(pitchGate, f.pitchGate);
        osc1UserPath.swapWith(f.osc1UserPath);
        osc2UserPath.swapWith(f.osc2UserPath);
        osc1UserIndex.store(f.osc1UserIndex.load());
        osc2UserIndex.store(f.osc2UserIndex.load());
        osc1SemiStepped.store(false, std::memory_order_relaxed);   // the modulators are cleared
        osc2SemiStepped.store(false, std::memory_order_relaxed);
    }

    // ── The render contract ──────────────────────────────────────────────────
    static constexpr int kNumModDests = kModDestCount;
    // The beat space the gates, envelopes and modulators run in: the longest gate pattern (16 bars).
    static constexpr double kBeatSpace = (double) GatePattern::kMaxPatternBars * 4.0;

    int  typeId() const override { return (int) mu_core::LayerTypeId::Pattern; }
    // Message thread, not processing: the insert (and the retire scratch) for this rate and block size.
    void prepare(double sampleRate, int blockSize) override;
    // Message thread: the slot's v{slot}_ rows, the tonal centre, the modulation inputs, and the slot's engine
    // and wavetable-bank lock from the host (the engine belongs to the slot: VoiceEngine::reset keeps the
    // oscillator phase, so a voice that moves keeps the slot's phase, as the per-slot engines always did).
    void bindParams(juce::AudioProcessorValueTreeState& apvts, int slot) override;
    // Audio thread: the voice's block into out[0, n) (handed over cleared): modulation, engine, gate, insert.
    void render(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n) override;
    // Audio thread: a spare above the live count keeps hearing the keys, so its Note-mode pitch is current
    // when it goes live again.
    void onParkedBlock(const mu_core::RenderContext& ctx) override;
    // The hot-swap boundary of this voice: its gate pattern's length.
    double loopBeats() const override { return (double) gate.patternLengthBars.load(std::memory_order_relaxed) * 4.0; }

    // ── Retire (a count-reducing full preset while playing; the spare stays in its slot) ────────
    // A 25 ms linear fade of the OLD config through the slot's engine and insert. The ramp is the bin's
    // (RetireState), applied before the insert, as the processor's retire fade always was.
    mu_core::RetirePolicy retirePolicy() const override;
    // Message thread, before any parameter of the slot changes: freeze the unmodulated config and the insert.
    void beginRetire() override;
    bool renderRetiring(const mu_core::RenderContext& ctx, juce::AudioBuffer<float>& out, int n,
                        mu_core::RetireState& rs) override;

    // ── Note mode (the layer's own held keys and pitch track) ──────────────────
    // 0 Free, 1 Note. Any thread (one atomic store); a change resets the held keys on the next block.
    void setNoteMode(int mode) noexcept { noteMode.store(mode, std::memory_order_relaxed); }
    // Message thread, at creation before the layer is rendered: the mode it starts in, already settled (no reset).
    void initNoteMode(int mode) noexcept { noteMode.store(mode, std::memory_order_relaxed); notes.lastMode = mode; notes.gateGain = mode == 1 ? 0.0f : 1.0f; }
    // When true the layer gates its own output with Note mode's 8 ms ramp (a host with no final-mix gate:
    // mu-Tronic). The mu-Tant standalone leaves it off and gates the final mix instead (decision A3).
    // Message thread, before publication.
    bool gatesOwnOutput = false;
    // Under layersLock (no block renders either layer): take `from`'s input state, the keys it holds and the
    // mode they were heard in. A layer built after the processor started hears no keys of its own until it is
    // published, so the host seeds it from a layer that has heard every block.
    void takeInputStateFrom(const Pattern& from) noexcept { notes = from.notes; inputSeeded = true; }
    bool hasInputState() const noexcept { return inputSeeded; }
    // Message thread: the layer starts with an input state of its own (it exists from the first block).
    void markInputSeeded() noexcept { inputSeeded = true; }

    // Under layersLock: the layer now sits in another slot (or was reset): its live-arc values and the held
    // modulated values (C12) belong to the slot it left.
    void resetSlotState() noexcept;

    // Post-matrix value of live-arc snapshot `snap` (MuTantModSnap.h) as the last rendered block resolved it.
    float getModLive(int snap) const noexcept
    {
        return snap >= 0 && snap < kTantSnapCount ? modLive[(size_t) snap].load(std::memory_order_relaxed) : 0.0f;
    }

private:
    // Takes `fresh`'s gate data into `live`. adoptDataFrom skips the exchange when the envelope's lock
    // stays busy, so it is retried a bounded number of times (each attempt is itself a capped spin);
    // only if every attempt fails does the reset fall back to clearing in place, which is unlocked. The
    // fallback is safe on the assumption that every writer of a gate pattern (editor edits, preset
    // load via deserialiseGate) runs on the message thread, the same thread as this reset, so none can
    // be mid-edit here; the audio thread is excluded by the caller.
    static void adoptGate(GatePattern& live, GatePattern& fresh) noexcept
    {
        constexpr int kAdoptAttempts = 8;
        for (int attempt = 0; attempt < kAdoptAttempts; ++attempt)
        {
            if (live.adoptDataFrom(fresh)) return;
            juce::Thread::yield();   // give the lock holder a turn before the next attempt
        }
        live.resetInPlaceTo(fresh);
    }

    void copyVoiceData(const Pattern& o)
    {
        // A skipped copy (the source envelope was being edited for too long) would leave stale data.
        [[maybe_unused]] const bool copied = gate.copyDataFrom(o.gate)
                                           & filterGate.copyDataFrom(o.filterGate)
                                           & pitchGate.copyDataFrom(o.pitchGate);
        jassert(copied);
        osc1UserPath = o.osc1UserPath;
        osc2UserPath = o.osc2UserPath;
        osc1UserIndex.store(o.osc1UserIndex.load());
        osc2UserIndex.store(o.osc2UserIndex.load());
    }

    // The render in phases (each one concern, in order).
    int         hearNotes(const mu_core::RenderContext& ctx) noexcept;   // the keys of this call; returns the mode
    VoiceConfig readConfig() const noexcept;                                 // the unmodulated config
    void        applyModulation(double beat, VoiceConfig& cfg, std::array<float, 4>& insP) noexcept;
    void        applyFilterEnvelope(bool playing, double beat, double sampleRate, int n, VoiceConfig& cfg) noexcept;
    void        applyPitchEnvelope(bool playing, double beat, VoiceConfig& cfg) noexcept;
    void        applyOwnNoteGate(juce::AudioBuffer<float>& out, int n, int mode, double sampleRate) noexcept;

    // The slot's rows, bound once per slot (render never looks a parameter up by id).
    struct Rows
    {
        mu_core::ParamBinding o1Oct, o1Semi, o1Fine, o1Pos, o1Wt, o2Oct, o2Semi, o2Fine, o2Pos, o2Wt;
        mu_core::ParamBinding xmodPhaseMode, xmodIndex, sync, xmodFdbk, xmodAmpMode, xmodDepth, xmodSsb;
        mu_core::ParamBinding o1Lvl, o2Lvl, noiseLvl, noiseType;
        mu_core::ParamBinding fltType, fltCut, fltRes, fltEnvDepth, fltDrv, fltLoCut;
        mu_core::ParamBinding flt2Type, flt2Cut, flt2Res, flt2EnvDepth, flt2Drv, flt2LoCut, fltSeries;
        mu_core::ParamBinding o1PenvDepth, o2PenvDepth, level, gateGap, gateBypass;
        mu_core::ParamBinding drvChar, insP1, insP2, insP3, insP4;
        mu_core::ParamBinding root, scale;
    } rows;

    // Modulation inputs for mu_mod::resolveLane: the target ids (proportion-space keys), each target's range and
    // backing atomic, and the seed/result map, pre-sized in bindParams (the audio thread only overwrites).
    std::array<const char*, kNumModDests>                    modIds {};
    std::array<juce::NormalisableRange<float>, kNumModDests> modRanges {};
    std::array<const std::atomic<float>*, kNumModDests>      modAtoms {};
    std::unordered_map<std::string_view, float>              modMap;

    // The slot's engine and the bank lock that guards the wavetables it reads (the host's; bound per slot).
    VoiceEngine*                 engine = nullptr;
    const juce::CriticalSection* bankLock = nullptr;

    // The UI's live modulation values, on their own cache line (C16): written by the audio thread.
    alignas(64) std::array<std::atomic<float>, kTantSnapCount> modLive {};

    // Note mode: the requested mode, and what the audio thread heard (the held keys, the mode they were heard
    // in, the own-output gate's ramp). Copied whole when a new layer is seeded.
    std::atomic<int> noteMode { 0 };
    struct NoteInput
    {
        HeldNoteStack held;
        int   lastMode = 0;
        float gateGain = 1.0f;
    } notes;
    bool inputSeeded = false;   // message thread: notes reflect every block since the processor started

    // The retire snapshot: the OLD unmodulated config and the insert, frozen by beginRetire.
    struct PatternRetire { VoiceConfig config; int insAlgo = 0; std::array<float, 4> insP {}; };
    mu_core::RetireSnapshot<PatternRetire> retireSnap;
    juce::AudioBuffer<float> tailScratch;   // a tail that must add rather than write renders here (sized at prepare)
};
JUCE_END_IGNORE_WARNINGS_MSVC

} // namespace mu_tant
