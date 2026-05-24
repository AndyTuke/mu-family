#include "InsertProcessor.h"
#include "InsertSlotConfig.h"

#include "Audio/FX/Insert/NoneInsert.h"
#include "Audio/FX/Insert/SoftClipInsert.h"
#include "Audio/FX/Insert/HardClipInsert.h"
#include "Audio/FX/Insert/FoldInsert.h"
#include "Audio/FX/Insert/BitcrusherInsert.h"
#include "Audio/FX/Insert/ClipperInsert.h"
#include "Audio/FX/Insert/EqInsert.h"
#include "Audio/FX/Insert/CompressorLimiterInsert.h"
#include "Audio/FX/Insert/RingModInsert.h"
#include "Audio/FX/Insert/TapeSatInsert.h"
#include "Audio/FX/Insert/KarplusStrongInsert.h"
#include "Audio/FX/Insert/VocoderInsert.h"

InsertProcessor::InsertProcessor()
{
    // pre-allocate every distinct algorithm up-front; the dispatch table
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

    // Compressor (7) + Limiter (8) share one CompressorLimiterInsert
    // instance. Matches the pre-refactor behaviour where both insertAlgo codes
    // operated on the same `compEnvelope[2]` state — switching between them
    // mid-bar carried the envelope across rather than resetting it, which is
    // audibly less surprising than a fresh envelope at the moment of switch.
    auto* compLim = add(std::make_unique<CompressorLimiterInsert>());
    dispatch[7]   = compLim;
    dispatch[8]   = compLim;

    dispatch[9]  = add(std::make_unique<RingModInsert>());
    dispatch[10] = add(std::make_unique<TapeSatInsert>());

    dispatch[11] = add(std::make_unique<KarplusStrongInsert>());
    dispatch[12] = add(std::make_unique<VocoderInsert>(false));
    dispatch[13] = add(std::make_unique<VocoderInsert>(true));
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
    // (0..5) where slot 3 is the LPF cutoff. EQ / Comp / Limiter / RingMod /
    // TapeSat / Karplus / Vocoder all consume slot 3 themselves (or repurpose
    // it as Note / Release / Freq / Tone) and shouldn't get a second LPF.
    if (p.insertAlgo >= 1 && p.insertAlgo <= 5 && currentSampleRate > 0.0)
    {
        const float lpfHz = mu_ui::normToActual(p.insertParam[3], p.insertAlgo, 3);
        if (lpfHz < 19000.0f)
        {
            for (int ch = 0; ch < nCh; ++ch)
                postDriveTone[ch].prepare(lpfHz, (float) currentSampleRate);

            for (int ch = 0; ch < nCh; ++ch)
            {
                auto* data = buf.getWritePointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = postDriveTone[ch].process(data[i]);
            }
        }
    }
}
