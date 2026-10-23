#pragma once

// One recorded Arp layer block sequence, shared by the two test targets that must agree on it: mu-toni-tests
// renders it through the real processor (the channel's pre-mixer stream) and mu-toni-layer-tests through
// Arp::render with a hand-built RenderContext and no processor at all. Both compare with kRecordedHash, so a
// layer that secretly needs the processor, or a processor that feeds the layer differently from the
// contract, fails one of them.

#include "Persistence/LayerParamIds.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mu_toni_fixture
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm        = 120.0;
    constexpr int    kBlock      = 512;
    constexpr int    kBlocks     = 48;

    // The sound: a fast minor-seventh arp with accents and a bright, resonant filter, so every stage of the
    // voice and the insert shows in the hash.
    inline void configure(juce::AudioProcessorValueTreeState& apvts, int slot)
    {
        auto set = [&](const char* suffix, float plain)
        {
            if (auto* p = apvts.getParameter(mu_toni::layerParamId(slot, suffix)))
                p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        set("rate", 7.0f);   set("gate", 65.0f);   set("chord", 9.0f);   set("octs", 3.0f);   set("dir", 40.0f);
        set("acc", 60.0f);   set("accLen", 3.0f);  set("accPat", 5.0f);  set("cut", 2200.0f); set("res", 0.6f);
        set("o2s", 7.0f);    set("xmod_index", 20.0f);  set("drvChar", 3.0f);  set("insP1", 0.4f);
    }

    // A key at block 6 (root by MIDI: D4), a second at block 20, both released by block 36.
    struct Key { int block; uint8_t status, note, velocity; };
    inline const std::vector<Key>& keys()
    {
        static const std::vector<Key> k { { 6, 0x90, 62, 100 }, { 20, 0x90, 69, 100 }, { 30, 0x80, 69, 0 }, { 36, 0x80, 62, 0 } };
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
    constexpr std::uint64_t kRecordedHash = 0x2e00d6a1c09216c9ull;
}
