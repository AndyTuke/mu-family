#pragma once

#include "Audio/FX/Insert/InsertAlgorithmBase.h"
#include "Audio/FX/Algorithms/FxUse.h"
#include "Audio/FX/Algorithms/InsertSupport.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace mu_audio
{
// insertAlgo = 28. Stutter: a tempo-locked repeater. The sound is recorded all the time; at every slice boundary a dice roll decides
// whether to freeze the slice just played and repeat it, then the insert returns to the live sound.
//   - Size       5..500 ms (log). With the insert's Sync on it is 1..16 sixteenth notes of the tempo (slow tempi cap at 1 s).
//   - Repeat     0..100 %: the chance that a boundary starts a stutter; 100 stutters continuously, 0 never (untouched).
//   - Direction  Fwd, Rev, Alt (forwards then backwards on every other pass).
//   - Mix        0..100 %: dry / wet.
// An insert receives only the tempo, not the transport position, so the clock is a free-running sample counter restarted at
// reset(): locked to the tempo but not to the bar. A hit repeats the slice for up to 4 passes (fewer for slices over about
// 0.4 s, so the frozen window is not overwritten while the ring keeps recording). Each pass edge fades over 2 ms (a quarter of
// the slice for tiny slices), so the loop point does not click. The roll and the clock are shared by both channels. No latency.
class StutterInsert : public InsertAlgorithmBase
{
public:
    static constexpr FxUse kUse = FxUse::Insert;

    void prepare(double sampleRate, int) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        // Two seconds of history; the slice cap and the pass count below keep a frozen window inside it.
        ringSize = (int) std::ceil(2.0 * sr) + 8;
        for (auto& r : ring) r.assign((size_t) ringSize, 0.0f);
        mixSm.reset(sr, 0.015);
        reset();
    }

    void setSlotIndex(int slotIndex) override
    {
        slotSeed = slotIndex;
        rng.seed(slotSeed);
    }

    void reset() override
    {
        for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
        writePos = 0;
        written = 0;
        sliceLen = 0;
        sinceBoundary = 0;
        passesLeft = 0;
        passesTotal = 0;
        holdLen = 0;
        holdEnd = 0;
        passPos = 0;
        primed = false;
        rng.seed(slotSeed);
        rolls = hits = 0;
    }

    // Counters for the tests: boundaries where the dice were thrown, and how many of those started a stutter.
    int rollCount() const noexcept { return rolls; }
    int hitCount()  const noexcept { return hits; }

    void process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                 const VoiceParams& p, float& /*grOut*/) override
    {
        using namespace insert_support;
        const int nChState = juce::jmin(nCh, 2);
        if (nChState <= 0 || ns <= 0 || ring[0].empty()) return;
        // Slot 0 = Size ms (or sixteenths when synced), 1 = Repeat 0..100 %, 2 = Direction 0..2, 3 = Mix 0..100 %.
        const float sizeMs = juce::jlimit(5.0f, 500.0f, finiteOr(insertSlot(p, 0), 125.0f));
        const float repeat = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 1), 50.0f) * 0.01f);
        const int   dir    = juce::jlimit(0, 2, (int) std::round(finiteOr(insertSlot(p, 2), 0.0f)));
        const float mix    = juce::jlimit(0.0f, 1.0f, finiteOr(insertSlot(p, 3), 100.0f) * 0.01f);
        if (! primed) { mixSm.setCurrentAndTargetValue(mix); primed = true; }
        mixSm.setTargetValue(mix);

        double wantedSec = (double) sizeMs * 0.001;
        if (p.insertSync)
            wantedSec = (double) mu_ui::insertSyncStepFromNorm(p.insertParam[0]) * mu_ui::insertSixteenthSeconds(tempoBpm);
        const int wantedLen = juce::jlimit(juce::jmax(1, (int) std::round(0.001 * sr)), (ringSize - 8) / 2,
                                           (int) std::round(wantedSec * sr));
        if (sliceLen <= 0) sliceLen = wantedLen;   // the first block after a reset starts its clock with the current setting

        auto chans = buf.getArrayOfWritePointers();
        // Per sample: record, advance the slice clock (throwing the dice at a boundary), and build the replayed or live output.
        for (int i = 0; i < ns; ++i)
        {
            float x[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < nChState; ++ch)
            {
                x[ch] = sane(chans[ch][i]);
                ring[ch][(size_t) writePos] = x[ch];
            }
            const float m = mixSm.getNextValue();

            if (passesLeft == 0)
            {
                // Free running: a boundary comes every slice; the size or tempo in force then takes effect.
                if (sinceBoundary >= sliceLen)
                {
                    sinceBoundary = 0;
                    sliceLen = wantedLen;
                    rollDice(repeat);
                }
            }
            else if (passPos >= holdLen)
            {
                // The end of a pass: start the next one, or finish the stutter and roll again at once.
                passPos = 0;
                if (--passesLeft == 0)
                {
                    sinceBoundary = 0;
                    sliceLen = wantedLen;
                    rollDice(repeat);
                }
            }

            if (passesLeft > 0)
            {
                const int pass = passesTotal - passesLeft;
                const bool backwards = dir == 1 || (dir == 2 && (pass & 1) != 0);
                const int j = passPos;
                // Fade weight of the replay: 2 ms in and out of every pass (a quarter of a tiny slice).
                const int fade = juce::jmin((int) std::round(0.002 * sr), holdLen / 4);
                float e = 1.0f;
                float liveW = 0.0f;
                if (fade > 0)
                {
                    e = juce::jmin(1.0f, juce::jmin((float) (j + 1) / (float) fade, (float) (holdLen - j) / (float) fade));
                    // The live signal shows through only where the stutter starts or ends; inner loop points dip to silence.
                    if ((pass == 0 && j < fade) || (passesLeft == 1 && j >= holdLen - fade))
                        liveW = 1.0f - e;
                }
                const int64_t absIdx = backwards ? holdEnd - 1 - j : holdEnd - holdLen + j;
                for (int ch = 0; ch < nChState; ++ch)
                {
                    const float stut = ring[ch][(size_t) ringIndex(absIdx)] * e + x[ch] * liveW;
                    chans[ch][i] = x[ch] + m * (stut - x[ch]);
                }
                ++passPos;
            }
            else
            {
                for (int ch = 0; ch < nChState; ++ch) chans[ch][i] = x[ch];   // live: untouched
                ++sinceBoundary;
            }
            writePos = writePos + 1 >= ringSize ? 0 : writePos + 1;
            ++written;
        }
    }

private:
    // One dice roll at a boundary: a hit freezes the slice just recorded and starts repeating it.
    void rollDice(float repeat) noexcept
    {
        ++rolls;
        if (repeat > 0.0f && rng.uniform() < repeat)
        {
            ++hits;
            holdLen = sliceLen;
            holdEnd = written + 1;   // one past the newest recorded sample (the current one is already in the ring)
            // The window [holdEnd - holdLen, holdEnd) is read for up to 4 passes while the ring keeps recording; a reversed
            // pass reads the newest samples last, so allow one extra slice of age.
            const int fit = (ringSize - 8) / juce::jmax(1, holdLen) - 1;
            passesTotal = juce::jlimit(1, 4, fit);
            passesLeft = passesTotal;
            passPos = 0;
        }
    }

    int ringIndex(int64_t absolute) const noexcept
    {
        int64_t r = absolute % ringSize;
        if (r < 0) r += ringSize;
        return (int) r;
    }

    double sr = 44100.0;
    int    ringSize = 0;
    std::vector<float> ring[2];
    int      writePos = 0;
    int64_t  written = 0;        // samples recorded so far; the sample being processed has absolute index `written`
    int      sliceLen = 0;       // samples between boundaries while no stutter is running
    int      sinceBoundary = 0;
    int      passesLeft = 0, passesTotal = 0, holdLen = 0, passPos = 0;
    int64_t  holdEnd = 0;
    int      slotSeed = 0;
    int      rolls = 0, hits = 0;
    bool     primed = false;
    insert_support::Xorshift32 rng;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSm;
};
}  // namespace mu_audio
