#include "InsertProcessor.h"
#include <cmath>
#include "Audio/InsertSlotConfig.h"

#include "Audio/FX/Algorithms/NoneInsert.h"
#include "Audio/FX/Algorithms/SoftClipInsert.h"
#include "Audio/FX/Algorithms/HardClipInsert.h"
#include "Audio/FX/Algorithms/FoldInsert.h"
#include "Audio/FX/Algorithms/BitcrusherInsert.h"
#include "Audio/FX/Algorithms/ClipperInsert.h"
#include "Audio/FX/Algorithms/EqInsert.h"
#include "Audio/FX/Algorithms/CompressorLimiterInsert.h"
#include "Audio/FX/Algorithms/RingModInsert.h"
#include "Audio/FX/Algorithms/TapeSatInsert.h"
#include "Audio/FX/Algorithms/KarplusStrongInsert.h"
#include "Audio/FX/Algorithms/VocoderInsert.h"
#include "Audio/FX/Algorithms/OverdriveInsert.h"
#include "Audio/FX/Algorithms/TapeDelayInsert.h"
#include "Audio/FX/Algorithms/TapeSpringInsert.h"
#include "Audio/FX/Algorithms/FxFactory.h"
#include "Audio/FX/Algorithms/ChorusFx.h"
#include "Audio/FX/Algorithms/FlangerFx.h"
#include "Audio/FX/Algorithms/PhaserFx.h"
#include "Audio/FX/Algorithms/PitchDelayInsert.h"
#include "Audio/FX/Algorithms/TremoloInsert.h"
#include "Audio/FX/Algorithms/AutoPanInsert.h"
#include "Audio/FX/Algorithms/GateInsert.h"
#include "Audio/FX/Algorithms/WaveshaperInsert.h"
#include "Audio/FX/Algorithms/CombInsert.h"
#include "Audio/FX/Algorithms/FreqShiftInsert.h"
#include "Audio/FX/Algorithms/FormantInsert.h"
#include "Audio/FX/Algorithms/StutterInsert.h"
#include "Audio/FX/Algorithms/LoFiInsert.h"

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

    dispatch[0]  = add(mu_audio::makeInsert<NoneInsert>());
    dispatch[1]  = add(mu_audio::makeInsert<SoftClipInsert>());
    dispatch[2]  = add(mu_audio::makeInsert<HardClipInsert>());
    dispatch[3]  = add(mu_audio::makeInsert<FoldInsert>());
    dispatch[4]  = add(mu_audio::makeInsert<BitcrusherInsert>());
    dispatch[5]  = add(mu_audio::makeInsert<ClipperInsert>());
    dispatch[6]  = add(mu_audio::makeInsert<EqInsert>());

    // Compressor (7) + Limiter (8) share one CompressorLimiterInsert
    // instance. Matches the pre-refactor behaviour where both insertAlgo codes
    // operated on the same `compEnvelope[2]` state — switching between them
    // mid-bar carried the envelope across rather than resetting it, which is
    // audibly less surprising than a fresh envelope at the moment of switch.
    auto* compLim = add(mu_audio::makeInsert<CompressorLimiterInsert>());
    dispatch[7]   = compLim;
    dispatch[8]   = compLim;

    dispatch[9]  = add(mu_audio::makeInsert<RingModInsert>());
    dispatch[10] = add(mu_audio::makeInsert<TapeSatInsert>());

    dispatch[11] = add(mu_audio::makeInsert<KarplusStrongInsert>());
    dispatch[12] = add(std::make_unique<VocoderInsert>(false));
    dispatch[13] = add(std::make_unique<VocoderInsert>(true));
    dispatch[14] = add(mu_audio::makeInsert<OverdriveInsert>());
    dispatch[15] = add(mu_audio::makeInsert<TapeDelayInsert>());
    dispatch[16] = add(mu_audio::makeInsert<TapeSpringInsert>());
    dispatch[17] = add(mu_audio::makeInsert<mu_audio::ChorusFx>());
    dispatch[18] = add(mu_audio::makeInsert<mu_audio::FlangerFx>());
    dispatch[19] = add(mu_audio::makeInsert<mu_audio::PhaserFx>());
    dispatch[20] = add(mu_audio::makeInsert<PitchDelayInsert>());
    dispatch[21] = add(mu_audio::makeInsert<TremoloInsert>());
    dispatch[22] = add(mu_audio::makeInsert<mu_audio::AutoPanInsert>());
    dispatch[23] = add(mu_audio::makeInsert<mu_audio::GateInsert>());
    dispatch[24] = add(mu_audio::makeInsert<mu_audio::WaveshaperInsert>());
    dispatch[25] = add(mu_audio::makeInsert<mu_audio::CombInsert>());
    dispatch[26] = add(mu_audio::makeInsert<mu_audio::FreqShiftInsert>());
    dispatch[27] = add(mu_audio::makeInsert<mu_audio::FormantInsert>());
    dispatch[28] = add(mu_audio::makeInsert<mu_audio::StutterInsert>());
    dispatch[29] = add(mu_audio::makeInsert<mu_audio::LoFiInsert>());
}

void InsertProcessor::prepare(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    for (auto& a : owned)
        if (a) a->prepare(sampleRate, blockSize);
    postDriveTone[0].reset();
    postDriveTone[1].reset();
}

void InsertProcessor::setSlotIndex(int slotIndex)
{
    for (auto& a : owned)
        if (a) a->setSlotIndex(slotIndex);
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
    {
        algo->tempoBpm = tempo.load(std::memory_order_relaxed);
        algo->process(buf, ns, nCh, p, gr);
    }
    grReduction.store(gr);

    // Post-drive 1-pole LP tone filter — only for the drive-family algorithms
    // (0..5) where slot 3 is the LPF cutoff. EQ / Comp / Limiter / RingMod /
    // TapeSat / Overdrive / Karplus / Vocoder all consume slot 3 themselves (or repurpose
    // it as Note / Release / Freq / Tone) and shouldn't get a second LPF.
    // State is kept for two channels; any further channel passes through unfiltered.
    const int nToneCh = juce::jmin(nCh, 2);
    bool toneApplied = false;
    if (p.insertAlgo >= 1 && p.insertAlgo <= 5 && currentSampleRate > 0.0)
    {
        const float lpfHz = mu_ui::normToActual(p.insertParam[3], p.insertAlgo, 3);
        if (lpfHz < 19000.0f)
        {
            toneApplied = true;
            for (int ch = 0; ch < nToneCh; ++ch)
                postDriveTone[ch].prepare(lpfHz, (float) currentSampleRate);

            for (int ch = 0; ch < nToneCh; ++ch)
            {
                auto* data = buf.getWritePointer(ch);
                for (int i = 0; i < ns; ++i)
                    data[i] = postDriveTone[ch].process(data[i]);
            }
        }
    }
    // Not filtering this block (tone fully open, or another algorithm): follow the signal so the filter does not
    // hold an old value and step when it is switched on again.
    if (! toneApplied && ns > 0)
        for (int ch = 0; ch < nToneCh; ++ch)
            postDriveTone[ch].setState(buf.getReadPointer(ch)[ns - 1]);

    // NaN / infinity containment at the source: several algorithms keep recursive state (filter, feedback line)
    // that one bad sample would latch forever, and the master probe alone could only silence the whole plugin.
    // Adding x * 0 over the block gives NaN if any sample is NaN or infinite and exactly zero otherwise (one
    // cheap pass, no branch per sample). On a hit this layer's block is silenced and the insert state cleared.
    float probe = 0.0f;
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* data = buf.getReadPointer(ch);
        for (int i = 0; i < ns; ++i)
            probe += data[i] * 0.0f;
    }
    if (! std::isfinite(probe))
    {
        for (int ch = 0; ch < nCh; ++ch)
            buf.clear(ch, 0, ns);
        reset();
    }
}
