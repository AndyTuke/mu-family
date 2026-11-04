#include "Sequencer/PhrasePlayback.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mu_tilate
{

void PhrasePlayback::prepare(double sampleRate, int maxBlock, bool shortWindow)
{
    player.prepare(sampleRate, maxBlock, shortWindow);
    layerFilter.prepare(sampleRate, std::max(maxBlock, kFilterPiece), 2);
    reset();
}

// Renders `len` frames of the player into `view`, then runs the layer's filter over them in short pieces: each piece takes the
// cutoff the layer sets, moved by the newest slice's offset and filter envelope, so the envelope sweeps the filter inside a block.
void PhrasePlayback::renderPiece(juce::AudioBuffer<float>& view, int len, const PhraseSettings& s) noexcept
{
    const int code = filterCodeOfChoice(s.filterType);
    if (code < 0) { player.render(view, len); return; }
    layerFilter.setType(code);
    for (int off = 0; off < len; off += kFilterPiece)
    {
        const int m = std::min(kFilterPiece, len - off);
        juce::AudioBuffer<float> sub(view.getArrayOfWritePointers(), view.getNumChannels(), off, m);
        player.render(sub, m);
        float octaves = 0.0f, resAdd = 0.0f;
        player.filterModulation(octaves, resAdd);
        layerFilter.setCutoff(juce::jlimit(20.0f, 20000.0f, s.cutoffHz * std::exp2(octaves)));
        layerFilter.setResonance(juce::jlimit(0.0f, 1.0f, s.resonance + resAdd));
        layerFilter.process(sub, m, std::min(2, view.getNumChannels()));
    }
}

void PhrasePlayback::reset() noexcept
{
    layerFilter.reset();
    player.cut();   // everything, the pre-fade too
    anchored = false;
    cutPending = false;
}

void PhrasePlayback::hardReset() noexcept
{
    layerFilter.reset();
    player.reset();
    anchored = false;
    cutPending = false;
}

SliceParams PhrasePlayback::paramsFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b,
                                      SliceRange& range) const noexcept
{
    // The hit's own settings are offsets on top of the layer's values: level, fades, pitch, pan and both envelopes add (each sum
    // is held to its control's range). They belong to this block, not to the slice it plays, so two hits of one slice can sound different.
    const SliceSettings& own = b.own;
    SliceParams p;
    p.own       = own;
    // The filter is the layer's, run over the layer's output (renderPiece); a slice voice itself does not filter.
    p.own.filterType = 0;
    p.own.pitchSemi = juce::jlimit(-24.0f, 24.0f, s.pitchSemi + own.pitchSemi);
    p.own.fineCents = juce::jlimit(-100.0f, 100.0f, s.fineCents + own.fineCents);
    p.own.pan       = juce::jlimit(-1.0f, 1.0f, s.pan + own.pan);
    p.own.ampA = juce::jmax(0.0f, s.ampA + own.ampA);   p.own.ampD = juce::jmax(0.0f, s.ampD + own.ampD);
    p.own.ampS = juce::jlimit(0.0f, 1.0f, s.ampS + own.ampS);   p.own.ampR = juce::jmax(0.0f, s.ampR + own.ampR);
    p.own.fltA = juce::jmax(0.0f, s.fltA + own.fltA);   p.own.fltD = juce::jmax(0.0f, s.fltD + own.fltD);
    p.own.fltS = juce::jlimit(0.0f, 1.0f, s.fltS + own.fltS);   p.own.fltR = juce::jmax(0.0f, s.fltR + own.fltR);
    p.own.filterEnvAmt = juce::jlimit(-1.0f, 1.0f, s.filterEnvAmt + own.filterEnvAmt);
    p.gain      = s.gain * juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, own.levelDb)) * juce::jlimit(0.0f, 1.0f, own.volume);
    p.fadeInMs  = juce::jmax(0.0f, s.fadeInMs + own.fadeInMs);
    p.fadeOutMs = juce::jmax(0.0f, s.fadeOutMs + own.fadeOutMs);
    p.reverse   = own.reverse;
    p.holdMs    = s.holdMs;
    p.strength  = s.strength;

    // The tempo ratio is read now, at the slice start (a Stretch ratio is latched per slice). The file's own sample
    // rate is part of the re-pitch rate: nothing is resampled on load.
    const double rateFromFile = loop.fileSampleRate / c.sampleRate;
    const double matchRatio   = (s.mode == TempoMode::Match && s.loopTempo > 0.0 && c.bpm > 0.0) ? c.bpm / s.loopTempo : 1.0;   // project speed / loop speed
    const double tempoRatio   = matchRatio / (double) juce::jlimit(0.25f, 4.0f, own.retime);   // the hit's own retime (dragged edge) on top
    p.rate = (float) rateFromFile;
    if (std::abs(tempoRatio - 1.0) > 1.0e-9)
    {
        // The stretcher keeps the slice in time; the pitch share (1 - stretchMix) is its transposition, so the mix only moves the pitch.
        // Pure re-pitch (or a file not at the host rate, which the stretcher cannot take) is a plain rate change.
        const double mix = juce::jlimit(0.0, 1.0, (double) s.stretchMix);
        if (mix < 1.0e-3 || std::abs(rateFromFile - 1.0) > 1.0e-6)
            p.rate = (float) (rateFromFile * tempoRatio);
        else
        {
            p.timeRatio = (float) (1.0 / tempoRatio);
            p.own.pitchSemi += (float) (12.0 * std::log2(tempoRatio) * (1.0 - mix));
        }
    }

    range = loop.slices->slice(b.slice);
    if (loop.sliceScale != 1.0)
    {
        range.start = (int) std::lround((double) range.start * loop.sliceScale);
        range.end   = (int) std::lround((double) range.end   * loop.sliceScale);
    }
    // The hit's own trim: the start of the slice skipped, in beats of the loop's own tempo.
    if (own.trimInBeats > 0.0f && s.loopTempo > 0.0)
    {
        const int skip = (int) std::lround((double) own.trimInBeats * 60.0 / s.loopTempo * loop.fileSampleRate * loop.sliceScale);
        range.start = std::min(range.start + skip, std::max(range.start, range.end - 2));
    }
    // A block made longer than its slice runs on into the loop after it: the range grows to cover the block's length (at the hit's own
    // speed), up to the end of the audio. A block cannot be longer than the loop has audio for.
    if (b.length > 0.0 && s.loopTempo > 0.0)
    {
        const double wantedBeats = b.length / std::max(0.25, (double) own.retime);   // loop beats the block consumes
        const int wanted = (int) std::lround(wantedBeats * 60.0 / s.loopTempo * loop.fileSampleRate * loop.sliceScale);
        range.end = std::max(range.end, std::min(loop.audio->getNumSamples(), range.start + wanted));
    }
    return p;
}

void PhrasePlayback::trigger(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b) noexcept
{
    // A hit whose slice was cropped off is silent, and still ends whatever sounds before it.
    if (b.slice < 0 || b.slice >= loop.slices->size()) { player.cutSlices(); return; }
    SliceRange range;
    const auto p = paramsFor(loop, s, c, b, range);
    player.trigger(loop.audio, range, p);
}

bool PhrasePlayback::preFor(const PhraseLoop& loop, const PhraseSettings& s, const PhraseClock& c, const TimelineBlock& b, Pre& pre) const noexcept
{
    if (b.slice < 0 || b.slice >= loop.slices->size() || ! b.own.preFade) return false;
    SliceRange whole;
    pre.params = paramsFor(loop, s, c, b, whole);
    const auto& own = pre.params.own;

    // The pre-fade is the first part of the slice played backwards, as long as asked (output time) or as the slice allows.
    const double step = std::max(0.01, (double) std::max(0.01f, pre.params.rate) * pitchRatio(own));
    const double wanted = (double) juce::jlimit(10.0f, 1000.0f, own.preLengthMs) * 0.001 * c.sampleRate * step;   // in source samples
    const int src = (int) std::min((double) whole.length(), wanted);
    if (src < 2) return false;
    // The pre-fade is the first part of what the slice will play: its start, or its end when the slice plays backwards.
    pre.range = pre.params.reverse ? SliceRange { whole.end - src, whole.end } : SliceRange { whole.start, whole.start + src };
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
        renderPiece(out, n, s);
        return;
    }

    examined = 0;
    const double L   = loop.timeline->lengthBeats();
    const double bps = c.beatsPerSample;

    // A transport jump (locate, host loop): re-anchor, the sounding slice leaves through the cut fade.
    bool fresh = ! anchored;   // a start or a jump: a block just behind the first sample (a start a hair past its beat) still fires
    if (anchored && std::abs(c.startBeat - expectedBeat) > std::max(2.0 * bps, 1.0e-6))
    {
        player.cut();
        cutPending = false;
        fresh = true;
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
        if (pos > L - 1.0e-9) pos = 0.0;   // a hair below the end is the downbeat: its block must fire, not be skipped
        const int toWrap = std::max(1, (int) std::ceil((L - pos) / bps - 1.0e-9));
        const int segEnd = std::min(n, done + toWrap);
        const double segEndPos = pos + (double) (segEnd - done) * bps;

        // The first block that starts at or after this segment's start (a block exactly at the start fires).
        const auto& blocks = loop.timeline->blocks();
        const auto startsAt = [&blocks](double beat) { return (int) (std::lower_bound(blocks.begin(), blocks.end(), beat, [](const TimelineBlock& b, double v) { return b.start < v; }) - blocks.begin()); };
        const double startTol = (fresh && done == 0) ? bps : 1.0e-9;
        int bi = startsAt(pos - startTol);   // the blocks are sorted by start

        // The blocks whose pre-fade can start inside this segment: the longest pre-fade is a second, so only blocks starting
        // within that of the segment (or, for the pre-fade that wraps back to the timeline's end, within that of its start).
        const double maxLead = c.sampleRate * bps + 1.0e-9;
        const int firstCandidate = startsAt(pos);
        const bool wrapCandidates = segEndPos > L - maxLead;

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
            auto consider = [&](int j)
            {
                ++examined;
                Pre pre;
                if (! preFor(loop, s, c, blocks[(size_t) j], pre)) return;
                double pb = blocks[(size_t) j].start - (double) pre.duration * bps;
                if (pb < 0.0) pb += L;
                if (pb < pos - 1.0e-9 || pb >= segEndPos - 1.0e-9) return;
                const int f = done + std::max(0, (int) std::ceil((pb - pos) / bps - 1.0e-9));
                if (f < at || f > preFrame) return;
                if (f < lastPreFrame || (f == lastPreFrame && j <= lastPreBlock)) return;
                if (f < preFrame || (f == preFrame && j < preBlock)) { preFrame = f; preBlock = j; }
            };
            for (int j = firstCandidate; j < (int) blocks.size() && blocks[(size_t) j].start <= segEndPos + maxLead; ++j) consider(j);
            if (wrapCandidates)
                for (int j = 0; j < (int) blocks.size() && blocks[(size_t) j].start <= maxLead; ++j) consider(j);
            if (preBlock >= 0 && preFrame <= nextFrame) { nextFrame = preFrame; what = PreFade; }

            if (nextFrame > at)
            {
                juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), out.getNumChannels(), at, nextFrame - at);
                renderPiece(view, nextFrame - at, s);
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
