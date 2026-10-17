// mu-toni voice tests — the wavetable voice (mu-Tant's oscillators via the shared
// XModOscPair) renders a note through every cross-mod mode, stays finite and audible,
// and falls silent after note-off.

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "Audio/ToniVoice.h"

using namespace mu_toni;

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 256;

// Render `blocks` blocks of a held note (or its tail) into a stereo buffer; peak level out.
float renderPeak(ToniVoice& v, int blocks, bool& finite)
{
    juce::AudioBuffer<float> buf(2, kBlock);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        v.process(buf, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = buf.getSample(ch, i);
                finite = finite && std::isfinite(s);
                peak = juce::jmax(peak, std::abs(s));
            }
    }
    return peak;
}
} // namespace

class ToniVoiceTest : public juce::UnitTest
{
public:
    ToniVoiceTest() : juce::UnitTest("Wavetable voice", "mu-toni") {}

    void runTest() override
    {
        mu_wavetable::WavetableBank bank;
        bank.loadFactoryBank();

        beginTest("A held note is audible and finite in every cross-mod mode");
        {
            for (int phaseMode = 0; phaseMode < 3; ++phaseMode)
                for (int ampMode = 0; ampMode < 3; ++ampMode)
                {
                    ToniVoice v;
                    v.setBank(&bank);
                    v.prepare(kSr, kBlock);
                    ToniVoiceParams p;
                    p.xmod.phaseMode = phaseMode;  p.xmod.index = 0.6f;
                    p.xmod.ampMode   = ampMode;    p.xmod.depth = 0.5f;  p.xmod.ssbHz = 120.0f;
                    p.xmod.sync      = (phaseMode == 2);  p.xmod.feedback = (ampMode == 1);
                    v.setParams(p);
                    v.noteOn(57);
                    bool finite = true;
                    const float peak = renderPeak(v, 40, finite);
                    expect(finite, "non-finite sample, phase " + juce::String(phaseMode) + " amp " + juce::String(ampMode));
                    expect(peak > 0.01f, "silent, phase " + juce::String(phaseMode) + " amp " + juce::String(ampMode));
                }
        }

        beginTest("Every factory wavetable sounds");
        {
            for (int t = 0; t < bank.numTables(); ++t)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1Table = p.osc2Table = t;
                v.setParams(p);
                v.noteOn(60);
                bool finite = true;
                expect(renderPeak(v, 20, finite) > 0.01f && finite, "table " + bank.tableName(t) + " silent or non-finite");
            }
        }

        beginTest("The voice falls silent after note-off");
        {
            ToniVoice v;
            v.setBank(&bank);
            v.prepare(kSr, kBlock);
            ToniVoiceParams p;
            p.ampR = 0.01f;
            v.setParams(p);
            v.noteOn(60);
            bool finite = true;
            renderPeak(v, 10, finite);
            v.noteOff();
            renderPeak(v, 20, finite);   // release tail (10 ms) has long finished
            expect(! v.isActive(), "voice still active after its release");
        }

        beginTest("Noise: same slot and salt is bit-identical on both channels and across blocks; another slot differs");
        {
            // Noise-only note on a fresh voice seeded for (slot, salt): two consecutive blocks, stereo.
            auto renderNoise = [&](int slot, juce::int64 salt)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.setSlotIndex(slot, salt);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1LevelDb = -60.0f;  p.osc2LevelDb = -60.0f;  p.noiseLevelDb = 0.0f;
                v.setParams(p);
                v.noteOn(60);
                juce::AudioBuffer<float> out(2, 2 * kBlock), blk(2, kBlock);
                for (int b = 0; b < 2; ++b)
                {
                    blk.clear();
                    v.process(blk, kBlock);
                    for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, b * kBlock, blk, ch, 0, kBlock);
                }
                return out;
            };
            auto identical = [&](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y, int len)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len; ++i)
                        if (x.getSample(ch, i) != y.getSample(ch, i)) return false;
                return true;
            };
            const auto a = renderNoise(1, 0), b = renderNoise(1, 0), other = renderNoise(2, 0);
            expect(a.getMagnitude(0, 0, kBlock) > 0.0f && a.getMagnitude(1, 0, kBlock) > 0.0f, "noise is audible on both channels");
            expect(identical(a, b, 2 * kBlock), "same slot gives bit-identical noise, both channels, both blocks");
            expect(! identical(a, other, kBlock), "different slots give different noise");
            bool repeats = true;   // the second block continues the stream rather than repeating the first
            for (int i = 0; i < kBlock; ++i) repeats = repeats && a.getSample(0, i) == a.getSample(0, kBlock + i);
            expect(! repeats, "the second block continues the sequence, it does not repeat the first");
        }

        beginTest("Noise: differing salts give different noise on the same slot");
        {
            auto firstBlock = [&](int slot, juce::int64 salt)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.setSlotIndex(slot, salt);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1LevelDb = -60.0f;  p.osc2LevelDb = -60.0f;  p.noiseLevelDb = 0.0f;
                v.setParams(p);
                v.noteOn(60);
                juce::AudioBuffer<float> blk(2, kBlock);
                blk.clear();
                v.process(blk, kBlock);
                return blk;
            };
            auto same = [&](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
            {
                for (int i = 0; i < kBlock; ++i)
                    if (x.getSample(0, i) != y.getSample(0, i)) return false;
                return true;
            };
            expect(! same(firstBlock(1, 111), firstBlock(1, 222)), "two instances (different salts) on one slot do not share noise");
            expect(same(firstBlock(1, 111), firstBlock(1, 111)), "the same salt repeats");
        }

        beginTest("Glide: a tied C4 to C5 slide reaches C5 on time and rises monotonically while Portamento is nudged every block");
        {
            // setParams used to re-seed the pitch ramp whenever Portamento changed, which dropped the destination of a slide in
            // flight; Glide is a modulation target, so any modulation or automation on it stranded the pitch half way.
            constexpr int kBlk = 64;   // 1.3 ms: fine enough to follow a 200 ms slide
            ToniVoice v;
            v.setBank(&bank);
            v.prepare(kSr, kBlk);

            ToniVoiceParams p;
            p.osc1Pos = 0.0f;            // frame 0 of Basic Shapes: a sine, one rising zero crossing per cycle
            p.osc2LevelDb = -60.0f;      // Osc 2 off
            p.osc1Fine = 0.0f;
            p.cutoff = 20000.0f;         // filter open, so the tone is a clean sine
            p.ampA = 0.001f;  p.ampD = 0.05f;  p.ampS = 1.0f;  p.ampR = 0.2f;   // flat sustain
            p.portamentoMs = 200.0f;
            v.setParams(p);
            v.noteOn(60);                // C4

            std::vector<float> out;
            juce::AudioBuffer<float> blk(2, kBlk);
            int blockIndex = 0;
            auto renderBlocks = [&](int blocks)
            {
                for (int i = 0; i < blocks; ++i, ++blockIndex)
                {
                    // The Portamento value wiggles by 1 ms each block, as a modulator on Glide would.
                    p.portamentoMs = 200.0f + (float) (blockIndex & 1);
                    v.setParams(p);
                    blk.clear();
                    v.process(blk, kBlk);
                    for (int s = 0; s < kBlk; ++s) out.push_back(blk.getSample(0, s));
                }
            };

            renderBlocks(75);                          // 100 ms at C4
            const int slideStart = (int) out.size();
            v.noteOnLegato(72);                        // tie to C5
            renderBlocks(300);                         // 400 ms: the slide (200 ms) and 200 ms at C5

            // Instantaneous frequency at every cycle: interpolated rising zero crossings, one period apart.
            std::vector<double> crossing;
            for (size_t i = 1; i < out.size(); ++i)
                if (out[i - 1] < 0.0f && out[i] >= 0.0f)
                    crossing.push_back((double) (i - 1) + (double) (-out[i - 1]) / (double) (out[i] - out[i - 1]));
            struct Cycle { double endSample, hz; };
            std::vector<Cycle> cycles;
            for (size_t k = 1; k < crossing.size(); ++k)
                cycles.push_back({ crossing[k], kSr / (crossing[k] - crossing[k - 1]) });

            const double c4 = 261.6256, c5 = 523.2511;
            const double settle = 2000.0;                              // skip the attack and the filter's first samples
            const double reachedBy = (double) slideStart + 0.230 * kSr;   // 230 ms after the tie
            int checkedBefore = 0, checkedAfter = 0;
            bool monotonic = true, startOk = true, endOk = true;
            double worstEndError = 0.0;
            for (size_t k = 0; k < cycles.size(); ++k)
            {
                const auto& c = cycles[k];
                if (c.endSample > settle && c.endSample < (double) slideStart - 100.0)
                {
                    ++checkedBefore;
                    startOk = startOk && std::abs(c.hz / c4 - 1.0) < 0.01;
                }
                if (c.endSample >= reachedBy)
                {
                    ++checkedAfter;
                    const double err = std::abs(c.hz / c5 - 1.0);
                    worstEndError = std::max(worstEndError, err);
                    endOk = endOk && err < 0.01;
                }
                // Never falls (0.5 % allows for the crossing interpolation and the block-stepped pitch).
                if (c.endSample > (double) slideStart && k > 0)
                    monotonic = monotonic && c.hz >= cycles[k - 1].hz * 0.995;
            }
            logMessage("glide: " + juce::String((int) cycles.size()) + " cycles, worst error at C5 "
                       + juce::String(worstEndError * 100.0, 3) + " %");
            expect(checkedBefore > 10 && checkedAfter > 20, "enough cycles were measured");
            expect(startOk, "the voice sits at C4 before the tie");
            expect(endOk, "the pitch is at C5 (within 1 %) 230 ms after the tie");
            expect(monotonic, "the pitch never falls during the slide");
        }

        beginTest("Envelope release: pushing unchanged settings every block does not cut a Sustain-0 tail");
        {
            // juce::ADSR::setParameters ends a release in progress when Sustain is 0, so a per-block setParams cut the tail.
            constexpr int kBlk = 256;
            ToniVoice v;
            v.setBank(&bank);
            v.prepare(kSr, kBlk);
            ToniVoiceParams p;
            p.ampA = 0.001f;  p.ampD = 0.5f;  p.ampS = 0.0f;  p.ampR = 1.0f;
            p.fA = 0.001f;    p.fD = 0.5f;    p.fS = 0.0f;    p.fR = 1.0f;
            p.pA = 0.001f;    p.pD = 0.5f;    p.pS = 0.0f;    p.pR = 1.0f;
            v.setParams(p);
            v.noteOn(60);

            juce::AudioBuffer<float> blk(2, kBlk);
            auto renderBlocks = [&](int blocks)
            {
                float peak = 0.0f;
                for (int i = 0; i < blocks; ++i)
                {
                    v.setParams(p);           // unchanged values, every block
                    blk.clear();
                    v.process(blk, kBlk);
                    peak = blk.getMagnitude(0, 0, kBlk);
                }
                return peak;
            };

            renderBlocks(19);                 // about 100 ms into the 0.5 s decay
            v.noteOff();                      // release: 1 s from the level the note had
            const float peakAt500 = renderBlocks(94);   // 0.5 s into the release
            const bool activeAt500 = v.isActive();
            renderBlocks(131);                // 1.2 s in
            logMessage("peak 0.5 s into the release: " + juce::String(peakAt500, 4));
            expect(activeAt500, "the amp release is still running 0.5 s after the note-off");
            expect(peakAt500 > 0.02f, "and it is still audible (peak " + juce::String(peakAt500, 4) + ")");
            expect(! v.isActive(), "the release has ended by 1.2 s");
        }

        beginTest("Envelope release: a Release change made during a tail is used when the next note starts");
        {
            constexpr int kBlk = 256;
            ToniVoice v;
            v.setBank(&bank);
            v.prepare(kSr, kBlk);
            ToniVoiceParams p1;
            p1.ampA = 0.001f;  p1.ampD = 0.01f;  p1.ampS = 1.0f;  p1.ampR = 0.05f;   // a 50 ms tail
            v.setParams(p1);
            v.noteOn(60);

            juce::AudioBuffer<float> blk(2, kBlk);
            auto renderBlocks = [&](int blocks)
            {
                float peak = 0.0f;
                for (int i = 0; i < blocks; ++i)
                {
                    blk.clear();
                    v.process(blk, kBlk);
                    peak = blk.getMagnitude(0, 0, kBlk);
                }
                return peak;
            };

            renderBlocks(9);                  // about 48 ms, at sustain
            v.noteOff();
            renderBlocks(2);                  // 10 ms into the 50 ms release
            ToniVoiceParams p2 = p1;
            p2.ampR = 0.8f;
            v.setParams(p2);                  // the change arrives mid-tail and must wait
            const float mid = renderBlocks(1);
            expect(mid > 0.01f && v.isActive(), "the change did not cut the tail");
            renderBlocks(20);                 // past the end of the old 50 ms tail
            expect(! v.isActive(), "the old tail ended");

            v.noteOn(60);                     // no setParams call: the held-back Release applies here
            renderBlocks(9);
            v.noteOff();
            const float at400 = renderBlocks(75);          // 0.4 s into the release
            const bool activeAt400 = v.isActive();
            renderBlocks(113);                              // 1.0 s in: the 0.8 s release has ended
            logMessage("peak 0.4 s into the second release: " + juce::String(at400, 4));
            expect(activeAt400 && at400 > 0.02f, "the new 0.8 s Release was not in use (peak " + juce::String(at400, 4) + " at 0.4 s)");
            expect(! v.isActive(), "the second release ran past its 0.8 s");
        }
    }
};

static ToniVoiceTest toniVoiceTest;
