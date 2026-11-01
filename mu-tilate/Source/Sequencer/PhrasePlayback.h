#pragma once

#include "Audio/MultiModeFilter.h"   // mu-core: the family's filter
#include "Audio/SlicePlayer.h"
#include "Sequencer/LayerFilterTypes.h"
#include "Sequencer/SliceList.h"
#include "Sequencer/Timeline.h"

#include <juce_audio_basics/juce_audio_basics.h>

// Plays a Phrase: the transport-anchored scheduler between the timeline and the slice player
// (docs/mu-tilate/design.md, "Clocking and tempo matching"). Every position it uses is a function of the call's
// start beat, so the output does not depend on the host block size; a jump of the transport re-anchors it (the
// sounding slice is cut through the declick fade).
namespace mu_tilate
{

enum class TempoMode { Off, Repitch, Stretch };

// The loop and its edits, read-only here (the layer swaps them in at a loop boundary).
struct PhraseLoop
{
    const juce::AudioBuffer<float>* audio = nullptr;
    double         fileSampleRate = 48000.0;   // the rate of `audio` as given (the host rate when a resampled copy is passed)
    double         sliceScale = 1.0;           // the slices are in the original file samples; this maps them to `audio`
    const SliceList* slices   = nullptr;
    const Timeline*  timeline = nullptr;
};

struct PhraseSettings
{
    TempoMode mode      = TempoMode::Off;
    double    loopTempo = 120.0;   // the loop's own tempo in BPM (Set override, metadata or analysis)
    float     holdMs    = 10.0f;   // Stretch: Transient hold
    float     strength  = 1.0f;    // Stretch: Strength
    float     fadeInMs  = 0.5f;
    float     fadeOutMs = 0.5f;
    float     gain      = 1.0f;
    int       filterType = 0;        // the layer's filter: 0 off, 1 low-pass, 2 high-pass, 3 band-pass
    float     cutoffHz   = 20000.0f;
    float     resonance  = 0.0f;     // 0..1
    // The layer's own pitch, pan and envelopes: a hit adds its offsets to these.
    float     pitchSemi  = 0.0f, fineCents = 0.0f, pan = 0.0f;
    float     ampA = 0.0f, ampD = 100.0f, ampS = 1.0f, ampR = 0.0f;
    float     filterEnvAmt = 0.0f;
    float     fltA = 0.0f, fltD = 100.0f, fltS = 1.0f, fltR = 0.0f;
};

// The transport as one call sees it (a subset of mu_core::RenderContext; the layer fills it from there).
struct PhraseClock
{
    bool   playing        = false;
    double startBeat      = 0.0;    // song beat at the call's first frame
    double beatsPerSample = 0.0;
    double bpm            = 120.0;
    double sampleRate     = 48000.0;
};

class PhrasePlayback
{
public:
    // Message thread, not processing (see SlicePlayer::prepare).
    void prepare(double sampleRate, int maxBlock, bool shortWindow = false);
    // Audio thread: forget the transport position and cut the sounding slice (a stop, a layer swap).
    void reset() noexcept;
    // Audio thread: drop every voice at once, with no fade. For a loop that was replaced (the voices point into the old
    // audio) or a slot that has just gone live; a stop uses reset.
    void hardReset() noexcept;

    // Audio thread: adds the call's audio into out[0, n).
    void render(const PhraseLoop& loop, const PhraseSettings& settings, const PhraseClock& clock,
                juce::AudioBuffer<float>& out, int n) noexcept;

    bool isActive() const noexcept { return player.isActive(); }
    // How far the newest sounding hit moves the layer's four insert knobs (fractions of their travel). False, with zeros, when none sounds.
    bool insertOffsets(float (&out)[4]) const noexcept { return player.insertOffsets(out); }
    // How many blocks the last render call looked at when scanning for pre-fades to start (a test of the scan's cost).
    int  preBlocksExamined() const noexcept { return examined; }

private:
    // The slice player's parameters for block `b` and the slice's range in the (possibly rescaled) audio.
    SliceParams paramsFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b, SliceRange& range) const noexcept;
    void trigger(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b) noexcept;

    // A block's pre-fade: its source range, its length in output samples and its parameters. False when the slice has none.
    struct Pre { SliceRange range; int duration = 0; SliceParams params; };
    bool preFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b, Pre& pre) const noexcept;

    // Renders `len` frames into `view`, the layer filter applied in pieces.
    void renderPiece(juce::AudioBuffer<float>& view, int len, const PhraseSettings& s) noexcept;

    static constexpr int kFilterPiece = 64;   // frames between updates of the layer filter's cutoff
    SlicePlayer player;
    MultiModeFilter layerFilter;               // the layer's filter (the family's sixteen types)
    bool   anchored    = false;   // expectedBeat is valid
    double expectedBeat = 0.0;    // where the next call should start if the transport ran on
    int    examined    = 0;       // see preBlocksExamined
    bool   cutPending  = false;   // a block with its own length is sounding: cut at cutBeat (a timeline position)
    double cutBeat     = 0.0;
};

} // namespace mu_tilate
