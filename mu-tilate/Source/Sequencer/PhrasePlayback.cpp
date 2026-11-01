#include "Sequencer/PhrasePlayback.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mu_tilate
{

void PhrasePlayback::prepare(double sampleRate, int maxBlock, bool shortWindow)
{
    player.prepare(sampleRate, maxBlock, shortWindow);
    reset();
}

void PhrasePlayback::reset() noexcept
{
    player.cut();   // everything, the pre-fade too
    anchored = false;
    cutPending = false;
}

void PhrasePlayback::hardReset() noexcept
{
    player.reset();
    anchored = false;
    cutPending = false;
}

SliceParams PhrasePlayback::paramsFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b,
                                      SliceRange& range) const noexcept
{
    // The slice's own settings sit on top of the layer's: its level adds (in dB), its fades add, and its reverse and the
    // block's reverse cancel each other.
    const SliceSettings& own = loop.slices->settings(b.slice);
    SliceParams p;
    p.own       = own;
    p.gain      = s.gain * juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 12.0f, own.levelDb));
    p.fadeInMs  = s.fadeInMs + own.fadeInMs;
    p.fadeOutMs = s.fadeOutMs + own.fadeOutMs;
    p.reverse   = b.reverse != own.reverse;
    p.holdMs    = s.holdMs;
    p.strength  = s.strength;

    // The tempo ratio is read now, at the slice start (a Stretch ratio is latched per slice). The file's own sample
    // rate is part of the re-pitch rate: nothing is resampled on load.
    const double rateFromFile = loop.fileSampleRate / c.sampleRate;
    const double tempoRatio   = (s.loopTempo > 0.0 && c.bpm > 0.0) ? c.bpm / s.loopTempo : 1.0;   // project speed / loop speed
    p.rate = (float) rateFromFile;
    if (s.mode == TempoMode::Repitch)
        p.rate = (float) (rateFromFile * tempoRatio);
    else if (s.mode == TempoMode::Stretch && std::abs(rateFromFile - 1.0) < 1.0e-6)   // the stretcher needs the file at the host rate
        p.timeRatio = (float) (1.0 / tempoRatio);

    range = loop.slices->slice(b.slice);
    if (loop.sliceScale != 1.0)
    {
        range.start = (int) std::lround((double) range.start * loop.sliceScale);
        range.end   = (int) std::lround((double) range.end   * loop.sliceScale);
    }
    return p;
}

void PhrasePlayback::trigger(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b) noexcept
{
    SliceRange range;
    const auto p = paramsFor(loop, s, c, b, range);
    player.trigger(loop.audio, range, p);
}

bool PhrasePlayback::preFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b, Pre& pre) const noexcept
{
    if (b.slice < 0 || b.slice >= loop.slices->size() || ! loop.slices->settings(b.slice).preFade) return false;
    SliceRange whole;
    pre.params = paramsFor(loop, s, c, b, whole);
    const auto& own = pre.params.own;

    // The pre-fade is the first part of the slice played backwards, as long as asked (output time) or as the slice allows.
    const double step = std::max(0.01, (double) pre.params.rate * std::exp2((double) (own.pitchSemi + own.fineCents / 100.0f) / 12.0));
    const double wanted = (double) juce::jlimit(10.0f, 1000.0f, own.preLengthMs) * 0.001 * c.sampleRate * step;   // in source samples
    const int src = (int) std::min((double) whole.length(), wanted);
    if (src < 2) return false;
    pre.range = { whole.start, whole.start + src };
    pre.duration = std::max(1, (int) std::lround((double) src / step));
    pre.params.gain *= juce::Decibels::decibelsToGain(juce::jlimit(-36.0f, 0.0f, own.preLevelDb));
    return true;
}

void PhrasePlayback::render(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c,
                            juce::AudioBuffer<float>& out, int n) noexcept
{
    // Not playing (or nothing to play): whatever is still sounding fades out, and the next start re-anchors.
    if (! c.playing || loop.audio == nullptr || loop.slices == nullptr || loop.timeline == nullptr
        || c.beatsPerSample <= 0.0 || loop.timeline->lengthBeats() <= 0.0)
    {
        if (anchored) { player.cut(); anchored = false; cutPending = false; }
        player.render(out, n);
        return;
    }

    const double L   = loop.timeline->lengthBeats();
    const double bps = c.beatsPerSample;

    // A transport jump (locate, host loop): re-anchor, the sounding slice leaves through the cut fade.
    if (anchored && std::abs(c.startBeat - expectedBeat) > std::max(2.0 * bps, 1.0e-6))
    {
        player.cut();
        cutPending = false;
    }
    anchored = true;
    expectedBeat = c.startBeat + (double) n * bps;

    // Phase 1: walk the call in segments that do not cross the timeline's end; each segment is rendered in pieces
    // between the triggers and cuts that fall inside it.
    int done = 0;
    while (done < n)
    {
        const double absBeat = c.startBeat + (double) done * bps;
        double pos = std::fmod(absBeat, L);
        if (pos < 0.0) pos += L;
        const int toWrap = std::max(1, (int) std::ceil((L - pos) / bps - 1.0e-9));
        const int segEnd = std::min(n, done + toWrap);
        const double segEndPos = pos + (double) (segEnd - done) * bps;

        // The first block that starts at or after this segment's start (a block exactly at the start fires).
        const auto& blocks = loop.timeline->blocks();
        int bi = 0;
        while (bi < (int) blocks.size() && blocks[(size_t) bi].start < pos - 1.0e-9) ++bi;

        int at = done;
        int lastPreFrame = -1, lastPreBlock = -1;   // the latest pre-fade started in this segment
        while (at < segEnd)
        {
            // The next event in the segment: a block start, or the pending cut, whichever is first.
            int nextFrame = segEnd;
            enum { None, Block, Cut, PreFade } what = None;
            if (bi < (int) blocks.size() && blocks[(size_t) bi].start < segEndPos - 1.0e-9)
            {
                const int f = done + std::max(0, (int) std::ceil((blocks[(size_t) bi].start - pos) / bps - 1.0e-9));
                if (f < nextFrame) { nextFrame = f; what = Block; }
            }
            if (cutPending && cutBeat >= pos - 1.0e-9 && cutBeat < segEndPos - 1.0e-9)
            {
                const int f = done + std::max(0, (int) std::ceil((cutBeat - pos) / bps - 1.0e-9));
                if (f <= nextFrame) { nextFrame = f; what = Cut; }   // a cut at the same frame as a block goes first
            }

            // A pre-fade starts its own length before its block (wrapping to the end of the timeline for a block near the start).
            // preBlock / preFrame order the ones that fall on the same frame, and never repeat one already started in this segment.
            int preBlock = -1, preFrame = std::numeric_limits<int>::max();
            for (int j = 0; j < (int) blocks.size(); ++j)
            {
                Pre pre;
                if (! preFor(loop, s, c, blocks[(size_t) j], pre)) continue;
                double pb = blocks[(size_t) j].start - (double) pre.duration * bps;
                if (pb < 0.0) pb += L;
                if (pb < pos - 1.0e-9 || pb >= segEndPos - 1.0e-9) continue;
                const int f = done + std::max(0, (int) std::ceil((pb - pos) / bps - 1.0e-9));
                if (f < at || f > preFrame) continue;
                if (f < lastPreFrame || (f == lastPreFrame && j <= lastPreBlock)) continue;
                if (f < preFrame || (f == preFrame && j < preBlock)) { preFrame = f; preBlock = j; }
            }
            if (preBlock >= 0 && preFrame <= nextFrame) { nextFrame = preFrame; what = PreFade; }

            if (nextFrame > at)
            {
                juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), out.getNumChannels(), at, nextFrame - at);
                player.render(view, nextFrame - at);
                at = nextFrame;
            }
            if (what == PreFade)
            {
                Pre pre;
                if (preFor(loop, s, c, blocks[(size_t) preBlock], pre)) player.triggerPre(loop.audio, pre.range, pre.params);
                lastPreFrame = preFrame; lastPreBlock = preBlock;
            }
            else if (what == Cut)
            {
                player.cutSlices();
                cutPending = false;
            }
            else if (what == Block)
            {
                const auto& b = blocks[(size_t) bi];
                trigger(loop, s, c, b);
                cutPending = b.length > 0.0;
                cutBeat = b.start + b.length;
                ++bi;
            }
            else
                break;
        }
        // The timeline wrapped inside this call: a pending cut that lay beyond the end is now relative to the next pass.
        if (cutPending && segEndPos >= L - 1.0e-9) cutBeat -= L;
        done = segEnd;
    }
}

} // namespace mu_tilate
