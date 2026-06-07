#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/MuOnChannels.h"   // Channel enum
#include "Audio/KickEngine.h"
#include "Audio/BassEngine.h"
#include "Audio/SampleChannel.h"

// GrooveVoices — owns the four instrument engines and routes the sequencer's triggers +
// the mixer's per-channel render hook to the right one. Engine params are pulled from the
// product APVTS each block (cached atomic pointers, no per-block string lookups). All
// engine state is audio-thread-only; the cached pointers are written by the message thread.
namespace mu_on
{

class GrooveVoices
{
public:
    void prepare(double sr, int maxBlock)
    {
        kick.prepare(sr);
        bass.prepare(sr, maxBlock);
        hat.prepare(sr, maxBlock, SampleChannel::HiHat);
        snare.prepare(sr, maxBlock, SampleChannel::Snare);
    }

    // Cache the engine param pointers once (after the APVTS exists).
    void cacheParams(juce::AudioProcessorValueTreeState& apvts)
    {
        auto P = [&apvts](const char* id) { return apvts.getRawParameterValue(id); };
        kTune = P("k_tune"); kPtch = P("k_ptch"); kPdec = P("k_pdec"); kAdec = P("k_adec"); kDrive = P("k_drive");
        bWave = P("b_wave"); bSub = P("b_sub"); bTune = P("b_tune"); bCut = P("b_cut"); bRes = P("b_res");
        bEnv = P("b_env");   bEdec = P("b_edec"); bAtk = P("b_atk"); bDec = P("b_dec"); bSus = P("b_sus"); bDrive = P("b_drive");
        hTune = P("h_tune"); hDec = P("h_dec"); sTune = P("s_tune"); sDec = P("s_dec");
    }

    // Push current APVTS values into the engines (call once per block, audio thread).
    void applyParams()
    {
        kick.setParams(g(kTune, 50.0f), g(kPtch, 220.0f), g(kPdec, 50.0f), g(kAdec, 180.0f), g(kDrive, 0.2f));
        bass.setParams(g(bTune, 41.2f), (int) g(bWave, 0.0f), g(bSub, 0.5f), g(bCut, 600.0f), g(bRes, 0.2f),
                       g(bEnv, 0.4f), g(bEdec, 180.0f), g(bAtk, 2.0f), g(bDec, 200.0f), g(bSus, 0.0f), g(bDrive, 0.2f));
        hat.setParams  (g(hTune, 0.0f), g(hDec, 60.0f));
        snare.setParams(g(sTune, 0.0f), g(sDec, 160.0f));
    }

    void trigger(int track, float velocity)
    {
        switch (track)
        {
            case Kick:  kick.trigger(velocity);  break;
            case Bass:  bass.trigger(velocity);  break;
            case Hat:   hat.trigger(velocity);   break;
            case Snare: snare.trigger(velocity); break;
            default: break;
        }
    }

    void render(int channel, juce::AudioBuffer<float>& buf, int n)
    {
        buf.clear();
        switch (channel)
        {
            case Kick:  kick.render(buf, n);  break;
            case Bass:  bass.render(buf, n);  break;
            case Hat:   hat.render(buf, n);   break;
            case Snare: snare.render(buf, n); break;
            default: break;
        }
    }

private:
    static float g(const std::atomic<float>* p, float fallback) noexcept
    {
        return p != nullptr ? p->load(std::memory_order_relaxed) : fallback;
    }

    KickEngine    kick;
    BassEngine    bass;
    SampleChannel hat, snare;

    const std::atomic<float>* kTune = nullptr; const std::atomic<float>* kPtch = nullptr;
    const std::atomic<float>* kPdec = nullptr; const std::atomic<float>* kAdec = nullptr; const std::atomic<float>* kDrive = nullptr;
    const std::atomic<float>* bWave = nullptr; const std::atomic<float>* bSub = nullptr; const std::atomic<float>* bTune = nullptr;
    const std::atomic<float>* bCut = nullptr;  const std::atomic<float>* bRes = nullptr; const std::atomic<float>* bEnv = nullptr;
    const std::atomic<float>* bEdec = nullptr; const std::atomic<float>* bAtk = nullptr; const std::atomic<float>* bDec = nullptr;
    const std::atomic<float>* bSus = nullptr;  const std::atomic<float>* bDrive = nullptr;
    const std::atomic<float>* hTune = nullptr; const std::atomic<float>* hDec = nullptr;
    const std::atomic<float>* sTune = nullptr; const std::atomic<float>* sDec = nullptr;
};

} // namespace mu_on
