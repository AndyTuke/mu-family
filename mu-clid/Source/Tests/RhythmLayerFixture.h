#pragma once

// One recorded Rhythm layer block sequence, shared by the two test targets that must agree on it: mu-clid-tests renders it
// through the real mu-Clid processor (the channel's pre-mixer stream) and mu-tronic-tests through a Rhythm in a mu-Tronic
// slot, where the layer clocks itself from the render context. Both compare with kRecordedHash, so a slot host that
// feeds the layer differently from mu-Clid's processor (its step clock, its hits, its modulation pass) fails one of them.

#include "Sequencer/Rhythm.h"
#include "Sequencer/ControlSequence.h"
#include "Modulation/ModulationAssignment.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mu_clid_fixture
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm        = 120.0;
    constexpr int    kBlock      = 512;
    constexpr int    kBlocks     = 384;   // about 4 s: two passes of the 16-step pattern

    // A one-second tone with a fast decay, so a hit is a short pitched pluck (deterministic bytes).
    inline juce::File writeSample(const juce::File& dir)
    {
        const int n = 48000;
        juce::AudioBuffer<float> tone(1, n);
        for (int i = 0; i < n; ++i)
            tone.setSample(0, i, 0.8f * std::exp(-4.0f * (float) i / (float) n)
                                      * std::sin(juce::MathConstants<float>::twoPi * 330.0f * (float) i / 48000.0f));
        auto file = dir.getChildFile("fixture-pluck.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (stream == nullptr) return {};
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions().withSampleRate(48000.0)
                                                                                     .withNumChannels(1)
                                                                                     .withBitsPerSample(16));
        if (writer == nullptr) return {};
        writer->writeFromAudioSampleBuffer(tone, 0, n);
        return file;
    }

    // The sound: a five-hit pattern in sixteen steps with an accent ring, pattern legato, a pitch offset, a resonant
    // filter and an amp envelope, and a stepped modulator on the filter cutoff, so the hit, the accent, the tie and the
    // modulation pass all show in the hash. `rhythm` is the layer the parameters belong to (the modulators live in it).
    inline void configure(juce::AudioProcessorValueTreeState& apvts, mu_core::Rhythm& rhythm, int slot)
    {
        auto set = [&](const char* suffix, float plain)
        {
            if (auto* p = apvts.getParameter("r" + juce::String(slot) + "_" + suffix))
                p->setValueNotifyingHost(p->convertTo0to1(plain));
        };
        set("stepsA", 16.0f); set("hitsA", 5.0f); set("rotA", 1.0f);
        set("stepsC", 4.0f);  set("hitsC", 1.0f); set("patLeg", 1.0f);
        set("pitchSemi", 3.0f);
        set("fltCut", 2500.0f); set("fltRes", 0.3f);
        set("aEnvDec", 0.25f); set("aEnvSus", 40.0f); set("aEnvRel", 0.3f);
        set("ampLvl", -3.0f); set("accentDb", 4.0f);

        auto& seq = rhythm.controlSequences[0];
        seq.mode = ControlSequence::Mode::Stepped;
        seq.polarity = ControlSequence::Polarity::Bipolar;
        seq.stepValues = { -60.0f, 40.0f, -20.0f, 80.0f };
        ModulationAssignment a;
        a.id = "fixture"; a.sourceId = "cs0_output"; a.destinationId = "filter.cutoff"; a.depth = 50.0f;
        [[maybe_unused]] const bool added = rhythm.modulationMatrix.addAssignment(a);
        jassert(added);
    }

    // FNV-1a 64 over the stream, block by block, each block left side then right side, every float's bits (the golden
    // harness's hash, mu-core Tests/GoldenRender.h; repeated here so the fixture needs no processor header). Also the peak.
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

    // The stream's hash as recorded by mu-Clid's own processor (both targets must reproduce it).
    constexpr std::uint64_t kRecordedHash = 0x5d279fcf16979021ull;
}
