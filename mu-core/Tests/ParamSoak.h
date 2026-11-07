#pragma once

#include "Tests/ProcessorSeamHelpers.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <random>

// A soak for a real processor, headless: a worker thread renders blocks in plugin mode with a running transport and random MIDI
// notes while the test thread, for MU_SOAK_SECONDS (default 5), changes random parameters (the extremes more often than a uniform
// draw), round-trips the host state, and runs the product's own structural edit (`structural`, optional). Any crash, assert or
// non-finite / runaway output fails the run; the seed is printed so a failure repeats. Message thread + one audio worker, like a host.
namespace mu_seam_test
{
    struct SoakOptions
    {
        unsigned seed = 1;
        int blockSize = 256;
        // One random structural edit on the message thread (add / remove / swap a layer, change a slot's type); may be empty.
        std::function<void(std::mt19937&)> structural;
        // Runs once after prepareToPlay, before the audio thread starts (load a loop, add a layer, ...).
        std::function<void()> setup;
        int stateRoundTripEvery = 25;   // a getStateInformation / setStateInformation pair every N actions (0 = never)
    };

    inline int soakSeconds()
    {
        const auto v = juce::SystemStats::getEnvironmentVariable("MU_SOAK_SECONDS", {});
        return v.isNotEmpty() ? juce::jlimit(1, 3600, v.getIntValue()) : 5;
    }

    // Four loud bursts half a second apart, two seconds of mono 16-bit audio: enough for a slicer to find slices.
    inline juce::File writeBurstLoop(const juce::File& dir)
    {
        const double sr = 48000.0;
        const int frames = 96000;
        juce::AudioBuffer<float> b(1, frames);
        b.clear();
        juce::Random r(5);
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < (int) (0.3 * sr); ++i)
                b.setSample(0, t * 24000 + i, 0.8f * std::exp(-(float) i / (0.03f * (float) sr)) * (r.nextFloat() * 2.0f - 1.0f));
        auto f = dir.getChildFile("soak-loop.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(f.createOutputStream());
        if (auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(sr).withNumChannels(1).withBitsPerSample(16)))
            writer->writeFromAudioSampleBuffer(b, 0, frames);
        return f;
    }

    // Runs the message loop for `ms` (a loop load completes on it); a no-op where modal loops are not permitted.
    inline void pumpMessages(int ms)
    {
       #if JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
       #else
        juce::ignoreUnused(ms);
       #endif
    }

    inline void runParamSoak(juce::UnitTest& t, ProcessorBase& p, const SoakOptions& o)
    {
        t.logMessage("soak seed " + juce::String((int) o.seed) + ", " + juce::String(soakSeconds()) + " s, block " + juce::String(o.blockSize));
        p.prepareToPlay(48000.0, o.blockSize);
        if (o.setup) o.setup();
        RunningPlayHead playHead(48000.0, o.blockSize);
        p.setPlayHead(&playHead);

        std::atomic<bool> stop { false }, insane { false };
        std::atomic<int> blocks { 0 };
        std::thread worker([&]
        {
            std::mt19937 wr(o.seed * 7919u + 3u);
            juce::AudioBuffer<float> buf(2, o.blockSize);
            juce::MidiBuffer midi;
            int held = -1;
            while (! stop.load(std::memory_order_acquire))
            {
                buf.clear(); midi.clear();
                // Every few blocks: release the held note and start another, on a random channel.
                if (blocks.load(std::memory_order_relaxed) % 5 == 0)
                {
                    if (held >= 0) midi.addEvent(juce::MidiMessage::noteOff(1 + (held >> 8), held & 0x7f), 0);
                    const int ch = (int) (wr() % 16), note = 24 + (int) (wr() % 72);
                    midi.addEvent(juce::MidiMessage::noteOn(1 + ch, note, (juce::uint8) (1 + wr() % 127)), (int) (wr() % (unsigned) o.blockSize));
                    held = (ch << 8) | note;
                }
                p.processBlock(buf, midi);
                if (! bufferIsSane(buf, 1000.0f)) insane.store(true);
                blocks.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::vector<juce::RangedAudioParameter*> params;
        for (auto* raw : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(raw)) params.push_back(r);

        const auto envSeed = juce::SystemStats::getEnvironmentVariable("MU_SOAK_SEED", {});
        std::mt19937 rng(envSeed.isNotEmpty() ? (unsigned) envSeed.getIntValue() : o.seed);
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) (soakSeconds() * 1000);
        int action = 0;
        while (juce::Time::getMillisecondCounter() < end)
        {
            ++action;
            const int roll = (int) (rng() % 100);
            if (roll < 85 && ! params.empty())
            {
                auto* prm = params[rng() % params.size()];
                const int kind = (int) (rng() % 10);
                const float v = kind < 2 ? 0.0f : kind < 4 ? 1.0f : (float) (rng() % 10001) / 10000.0f;
                prm->setValueNotifyingHost(v);
            }
            else if (o.structural && roll < 97) o.structural(rng);
            else if (o.stateRoundTripEvery > 0)
            {
                juce::MemoryBlock mb;
                p.getStateInformation(mb);
                p.setStateInformation(mb.getData(), (int) mb.getSize());
            }
           #if JUCE_MODAL_LOOPS_PERMITTED
            if (action % 40 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(5);   // timers, async updates
           #endif
            if (action % 200 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        const int before = blocks.load();
        const auto settle = juce::Time::getMillisecondCounter() + 5000;
        while (blocks.load() < before + 3 && juce::Time::getMillisecondCounter() < settle) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const bool alive = blocks.load() >= before + 3;
        stop.store(true);
        worker.join();
        p.setPlayHead(nullptr);
        t.expect(alive, "the audio thread was still rendering after " + juce::String(action) + " actions (a stall means a lock was lost)");
        t.expect(! insane.load(), "the output stayed finite and below 1000 through " + juce::String(action) + " random actions");
    }
}
