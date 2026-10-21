#pragma once

// One recorded pair of Track layer streams, shared by the two test targets that must agree on it: mu-on-tests
// renders it through the real processor (channels 0 and 1, the pre-mixer streams) and mu-on-layer-tests through
// Track::render with hand-built RenderContexts and no processor at all. Track 1 is a Rumble on track 0, so the
// pair also proves the peer read. Both compare with kRecordedHash, so a layer that secretly needs the processor,
// or a processor that feeds the layer differently from the contract, fails one of them.

#include "Persistence/TrackParams.h"
#include "Sequencer/Track.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace mu_on_fixture
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm        = 120.0;
    constexpr int    kBlock      = 512;
    constexpr int    kBlocks     = 64;

    // The sound: track 0 a driven, resonant saw Bass on a syncopated row with accents, swing and a lowered
    // accent; track 1 a Rumble on track 0 with every stage audible.
    inline void configure(juce::AudioProcessorValueTreeState& apvts)
    {
        auto set = [&](const juce::String& id, float plain)
        {
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        using mu_on::EngineType;
        set(mu_on::layerParamId(0, "eng"), (float) mu_on::toIndex(EngineType::Bass));
        set(mu_on::layerParamId(1, "eng"), (float) mu_on::toIndex(EngineType::Rumble));
        set(mu_on::layerParamId(1, "src"), 0.0f);
        set(mu_on::engineParamId(0, EngineType::Bass, "wave"), 1.0f);
        set(mu_on::engineParamId(0, EngineType::Bass, "cut"), 500.0f);
        set(mu_on::engineParamId(0, EngineType::Bass, "res"), 0.5f);
        set(mu_on::engineParamId(0, EngineType::Bass, "drive"), 0.4f);
        set(mu_on::engineParamId(0, EngineType::Bass, "sus"), 0.3f);
        set(mu_on::engineParamId(1, EngineType::Rumble, "drive"), 0.5f);
        set(mu_on::engineParamId(1, EngineType::Rumble, "revmix"), 0.4f);
        set(mu_on::layerParamId(0, "swing"), 0.3f);
        set(mu_on::layerParamId(0, "accent"), 0.6f);
    }

    // Track 0's steps (track 1, a Rumble, does not step).
    inline void configureRow(mu_on::Track& t)
    {
        const char* on = "1010001010100110";
        const char* ac = "1000001000000100";
        for (int s = 0; s < 16; ++s) { t.row.setOn(s, on[s] == '1'); t.row.setAccent(s, ac[s] == '1'); }
    }

    // FNV-1a 64 over the streams, block by block, each block track 0 then track 1, left side then right side,
    // every float's bits (the golden harness's hash, repeated here so the library-only target needs no processor
    // header). Also the streams' peak.
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

    // The streams' hash as recorded (both targets must reproduce it).
    constexpr std::uint64_t kRecordedHash = 0x2c7dc78498ada7deull;

    // ── One non-Rumble Track alone ───────────────────────────────────────────────────────────────────────────────
    // The same scenario on slot 0 for each of the four engines a mu-Tronic slot can lock (Kick, Bass, Hat, Snare): the
    // real mu-On processor (mu-on-tests, an unlocked track told its engine by `t0_eng`) and a mu-Tronic slot of the
    // type (mu-tronic-tests, a locked track) must both hash channel 0's pre-mixer stream, kBlocks blocks long, to
    // kSingleHash[engine index]. Rows as configureRow sets them; the same swing and accent.
    inline void configureSingle(juce::AudioProcessorValueTreeState& apvts, mu_on::EngineType engine)
    {
        auto set = [&](const juce::String& id, float plain)
        {
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        using mu_on::EngineType;
        set(mu_on::layerParamId(0, "eng"), (float) mu_on::toIndex(engine));
        switch (engine)
        {
            case EngineType::Kick:
                set(mu_on::engineParamId(0, engine, "tune"), 55.0f);
                set(mu_on::engineParamId(0, engine, "adec"), 300.0f);
                set(mu_on::engineParamId(0, engine, "drive"), 0.5f);
                break;
            case EngineType::Bass:
                set(mu_on::engineParamId(0, engine, "wave"), 1.0f);
                set(mu_on::engineParamId(0, engine, "cut"), 500.0f);
                set(mu_on::engineParamId(0, engine, "res"), 0.5f);
                set(mu_on::engineParamId(0, engine, "drive"), 0.4f);
                break;
            case EngineType::Hat:
                set(mu_on::engineParamId(0, engine, "tune"), 3.0f);
                set(mu_on::engineParamId(0, engine, "dec"), 120.0f);
                break;
            case EngineType::Snare:
                set(mu_on::engineParamId(0, engine, "tune"), -2.0f);
                set(mu_on::engineParamId(0, engine, "dec"), 250.0f);
                break;
            case EngineType::Rumble:
            case EngineType::FmPerc: break;
        }
        set(mu_on::layerParamId(0, "swing"), 0.3f);
        set(mu_on::layerParamId(0, "accent"), 0.6f);
    }

    // The hash of channel 0's stream for each engine, indexed by EngineType (Rumble has none: it processes another track).
    constexpr std::uint64_t kSingleHash[4] = { 0x215e638e585324c9ull, 0x07a54ecc63020cfdull, 0x537b9c40d4de34b1ull, 0xe84aea84dfaf666dull };   // Kick, Bass, Hat, Snare
}
