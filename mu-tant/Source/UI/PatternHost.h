#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// What the Pattern layer needs from whoever hosts it (docs/mu-tronic/design.md 2.2): for its UI
// (PatternContent) the parameters, the layer objects, the live modulation values, the insert meter, the
// transport and the user wavetables; for the layer itself, on the message thread, the shared wavetable bank
// a user wavetable path resolves into. mu-Tant's PluginProcessor implements it; a host of several layer types
// implements one per type. The method names carry the type, so one class can implement several hosts.
// Message thread only: the content's timer and controls, and the layer's persistence, call these.
namespace mu_tant
{

struct Pattern;
class VoiceEngine;

class PatternHost
{
public:
    virtual ~PatternHost() = default;

    // The parameter tree the layer's v{N}_ rows and the tonal centre (root, scale) live in.
    virtual juce::AudioProcessorValueTreeState& getPatternParameters() noexcept = 0;
    // Layer `layer` as its Pattern (envelopes, modulation matrix, user wavetables).
    virtual Pattern& getPatternLayer(int layer) noexcept = 0;
    // Post-matrix value of live-arc snapshot `snap` (MuTantModSnap.h) on layer `layer`, in the knob's units.
    virtual float getPatternModLive(int layer, int snap) const noexcept = 0;
    // The layer's insert gain-reduction meter, or null when it shows none.
    virtual const std::atomic<float>* getPatternInsertGainReduction(int layer) const = 0;
    // The transport as the content shows it: playing, and the beat it is at.
    virtual bool   isPatternTransportPlaying() const noexcept = 0;
    virtual double getPatternTransportBeat() const noexcept = 0;

    // User wavetables: the folder the browser opens, an oscillator's loaded file ("" = the factory selection),
    // whether that file has gone, and loading / clearing one (oscIndex 0 or 1).
    virtual juce::File   getPatternWavetablesDir() const = 0;
    virtual juce::String getPatternWavetablePath(int layer, int oscIndex) const = 0;
    virtual bool         isPatternWavetableMissing(int layer, int oscIndex) const = 0;
    virtual void         loadPatternWavetable(int layer, int oscIndex, const juce::File& file) = 0;
    virtual void         clearPatternWavetable(int layer, int oscIndex) = 0;

    // A saved user wavetable path as an index into the shared bank (-1: none, or the file is unreadable). The
    // layer's state load calls it; a table a staged preset pre-loaded resolves without taking the bank lock.
    virtual int resolvePatternWavetable(const juce::String& path) = 0;

    // Bound by the layer in bindParams (message thread), then used by its render (audio thread): the engine that
    // belongs to slot `slot` (the host keeps one per slot, so a voice that moves takes up the slot's oscillator
    // phase, as the per-slot engines always did) and the lock that guards the wavetable bank it reads (the
    // audio thread only try-locks it; null when the bank is never appended to while rendering).
    virtual VoiceEngine*                 getPatternEngine(int slot) noexcept = 0;
    virtual const juce::CriticalSection* getPatternBankLock() const noexcept = 0;
};

} // namespace mu_tant
