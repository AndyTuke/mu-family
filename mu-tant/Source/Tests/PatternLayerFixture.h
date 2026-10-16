#pragma once

// One recorded Pattern layer block sequence, shared by the two test targets that must agree on it: mu-tant-tests
// renders it through the real processor (the channel's pre-mixer stream) and mu-tant-layer-tests through
// Pattern::render with a hand-built RenderContext and no processor at all. Both compare with kRecordedHash, so a
// layer that secretly needs the processor, or a processor that feeds the layer differently from the contract,
// fails one of them.

#include "Persistence/PatternParams.h"
#include "Sequencer/Pattern.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mu_tant_fixture
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm        = 120.0;
    constexpr int    kBlock      = 512;
    constexpr int    kBlocks     = 96;

    // The sound: two detuned wavetables with cross-mod, a resonant filter, a one-bar gate with a filter
    // envelope, a tape insert and Note mode's pitch track, so every stage of the layer shows in the hash.
    inline void configure(juce::AudioProcessorValueTreeState& apvts, mu_tant::Pattern& pattern, int slot)
    {
        auto set = [&](const char* suffix, float plain)
        {
            if (auto* p = apvts.getParameter(mu_tant::layerParamId(slot, suffix)))
                p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        set("o1_wt", 1.0f);   set("o2_wt", 3.0f);   set("o2_semi", 7.0f);   set("xmod_index", 30.0f);
        set("flt_cut", 2500.0f); set("flt_res", 0.4f); set("flt_env_depth", 0.6f);
        set("drvChar", 10.0f); set("insP1", 0.6f);  set("insP2", 0.4f);  set("level", -3.0f);
        pattern.gate.patternLengthBars.store(1);
        pattern.gate.addEnvelope({ 0, 6, 0.2f, 0.0f, 0.0f, false });
        pattern.gate.addEnvelope({ 8, 6, 0.6f, 0.3f, 0.0f, false });
        pattern.filterGate.patternLengthBars.store(1);
        pattern.filterGate.addEnvelope({ 0, 12, 0.1f, 0.0f, 0.0f, false });
    }

    // Note mode on; a key at block 4, a second on top at block 40, released at 70 (back to the first), then 90.
    struct Key { int block; uint8_t status, note, velocity; };
    inline const std::vector<Key>& keys()
    {
        static const std::vector<Key> k { { 4, 0x90, 50, 100 }, { 40, 0x90, 57, 100 }, { 70, 0x80, 57, 0 }, { 90, 0x80, 50, 0 } };
        return k;
    }

    // FNV-1a 64 over the stream, block by block, each block left side then right side, every float's bits (the
    // golden harness's hash, mu-core Tests/GoldenRender.h; repeated here so the library-only target needs no
    // processor header). Also the stream's peak.
    struct StreamHash
    {
        std::uint64_t h = 14695981039346656037ull;
        float peak = 0.0f;
        void add(const juce::AudioBuffer<float>& b, int n, int offset = 0)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v = b.getSample(c, offset + i);
                    std::uint32_t bits;
                    std::memcpy(&bits, &v, 4);
                    for (int s = 0; s < 32; s += 8) { h ^= (std::uint8_t) (bits >> s); h *= 1099511628211ull; }
                    peak = std::max(peak, std::abs(v));
                }
        }
    };

    // The stream's hash as recorded (both targets must reproduce it).
    constexpr std::uint64_t kRecordedHash = 0x4f2fd5d76786a83full;
}
