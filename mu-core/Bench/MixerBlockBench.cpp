// mu-core-bench: wall time per MixerEngine::processBlock for one mixer shape (layers, buses, inserts), all layers active.
// The guard for the runtime-counts work (docs/design-runtime-counts.md section 5): the baseline is recorded before a
// phase and every phase that touches the audio path must stay within noise of it. Built per shape (a variant of mu-core
// today; one executable with the shape as arguments once counts are runtime).
//
//   mu-core-bench-<shape> [blocks] [warmup]       prints one JSON line per block size
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
#include "BusTestRig.h"

namespace
{
    struct Result { double medianUs, p99Us, bestUs; };

    // Run `blocks` blocks of `size` samples through a fresh mixer and time each one.
    Result run(int size, int blocks, int warmup)
    {
        bus_test::Rig rig(mu_limits::kMaxLayers);
        for (int ch = 0; ch < mu_limits::kMaxLayers; ++ch)
        {
            rig.dc[(size_t) ch]   = 0.01f * (float) (ch + 1);
            rig.tone[(size_t) ch] = 0.2f;
        }
        rig.mixer.prepare(bus_test::kRate, size);
        rig.out.setSize(2, size);

        std::vector<double> us;
        us.reserve((size_t) blocks);
        for (int b = 0; b < blocks + warmup; ++b)
        {
            const auto t0 = std::chrono::steady_clock::now();
            rig.mixer.processBlock(rig.out, rig.numActive, nullptr, rig.fx, size, nullptr, nullptr, nullptr, &rig.render);
            const auto t1 = std::chrono::steady_clock::now();
            rig.samplePos += size;
            if (b >= warmup) us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        std::sort(us.begin(), us.end());
        return { us[us.size() / 2], us[(size_t) ((double) us.size() * 0.99)], us.front() };
    }
}

int main(int argc, char** argv)
{
    const int blocks = argc > 1 ? std::atoi(argv[1]) : 5000;
    const int warmup = argc > 2 ? std::atoi(argv[2]) : 500;
    for (int size : { 256, 64 })
    {
        // Three interleaved passes, best median kept (the method in the design doc).
        Result best { 1e18, 1e18, 1e18 };
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto r = run(size, blocks, warmup);
            best.medianUs = std::min(best.medianUs, r.medianUs);
            best.p99Us    = std::min(best.p99Us, r.p99Us);
            best.bestUs   = std::min(best.bestUs, r.bestUs);
        }
        std::printf("{\"layers\":%d,\"buses\":%d,\"busInserts\":%d,\"block\":%d,\"medianUs\":%.3f,\"p99Us\":%.3f,\"bestUs\":%.3f}\n",
                    mu_limits::kMaxLayers, mu_limits::kMaxBuses, mu_limits::kBusInserts, size, best.medianUs, best.p99Us, best.bestUs);
    }
    return 0;
}
