#include "InsertProcessor.h"

#include "Audio/Processing/InsertFX/NoneInsert.h"
#include "Audio/Processing/InsertFX/SoftClipInsert.h"
#include "Audio/Processing/InsertFX/HardClipInsert.h"
#include "Audio/Processing/InsertFX/FoldInsert.h"
#include "Audio/Processing/InsertFX/BitcrusherInsert.h"
#include "Audio/Processing/InsertFX/ClipperInsert.h"
#include "Audio/Processing/InsertFX/EqInsert.h"
#include "Audio/Processing/InsertFX/CompressorLimiterInsert.h"
#include "Audio/Processing/InsertFX/RingModInsert.h"
#include "Audio/Processing/InsertFX/TapeSatInsert.h"
#include "Audio/Processing/InsertFX/KarplusStrongInsert.h"
#include "Audio/Processing/InsertFX/VocoderInsert.h"

InsertProcessor::InsertProcessor()
{
    // #425: pre-allocate every distinct algorithm up-front; the dispatch table
    // then points at these. owned is reserved so the raw pointers in dispatch
    // remain stable across the push_back calls (no reallocation).
    owned.reserve(kNumInsertAlgos);
    auto add = [&](std::unique_ptr<InsertAlgorithmBase> a) -> InsertAlgorithmBase*
    {
        auto* raw = a.get();
        owned.push_back(std::move(a));
        return raw;
    };

    dispatch[0]  = add(std::make_unique<NoneInsert>());
    dispatch[1]  = add(std::make_unique<SoftClipInsert>());
    dispatch[2]  = add(std::make_unique<HardClipInsert>());
    dispatch[3]  = add(std::make_unique<FoldInsert>());
    dispatch[4]  = add(std::make_unique<BitcrusherInsert>());
    dispatch[5]  = add(std::make_unique<ClipperInsert>());
    dispatch[6]  = add(std::make_unique<EqInsert>());

    // #425: Compressor (7) + Limiter (8) share one CompressorLimiterInsert
    // instance. Matches the pre-refactor behaviour where both insertAlgo codes
    // operated on the same `compEnvelope[2]` state — switching between them
    // mid-bar carried the envelope across rather than resetting it, which is
    // audibly less surprising than a fresh envelope at the moment of switch.
    auto* compLim = add(std::make_unique<CompressorLimiterInsert>());
    dispatch[7]   = compLim;
    dispatch[8]   = compLim;

    dispatch[9]  = add(std::make_unique<RingModInsert>());
    dispatch[10] = add(std::make_unique<TapeSatInsert>());

    // #422 / #423
    dispatch[11] = add(std::make_unique<KarplusStrongInsert>());
    dispatch[12] = add(std::make_unique<VocoderInsert>());
}

void InsertProcessor::prepare(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    for (auto& a : owned)
        if (a) a->prepare(sampleRate, blockSize);
    postDriveTone[0].reset();
    postDriveTone[1].reset();
}

void InsertProcessor::reset()
{
    for (auto& a : owned)
        if (a) a->reset();
    postDriveTone[0].reset();
    postDriveTone[1].reset();
}

void InsertProcessor::process(juce::AudioBuffer<float>& buf, int ns, int nCh,
                              const VoiceParams& p)
{
    const int idx = juce::jlimit(0, kNumInsertAlgos - 1, (int) p.insertAlgo);

    float gr = 0.0f;
    if (auto* algo = dispatch[(size_t) idx])
        algo->process(buf, ns, nCh, p, gr);
    grReduction.store(gr);

    // Post-drive 1-pole LP tone filter — only for the drive-family algorithms
    // (0..5) where p.insertTone is a cutoff. EQ (6) / Comp (7) / Limiter (8) /
    // RingMod (9) / TapeSat (10) repurpose insertTone for other meanings and
    // skip this step.
    if (p.insertAlgo < 6 && p.insertTone < 19000.0f && currentSampleRate > 0.0)
    {
        for (int ch = 0; ch < nCh; ++ch)
            postDriveTone[ch].prepare(p.insertTone, (float) currentSampleRate);

        for (int ch = 0; ch < nCh; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < ns; ++i)
                data[i] = postDriveTone[ch].process(data[i]);
        }
    }
}
